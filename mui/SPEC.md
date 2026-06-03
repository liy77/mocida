# MUI — language specification (draft v0)

> **Status:** design draft (M0). Nothing here is implemented yet; this fixes
> the surface so the parser, runtime, and codegen can be built against a
> stable target. Decisions captured: **view-function** components,
> **`.mui` + `.crm`** file split, **dual-mode** execution (interpret/JIT in
> dev, transpile in release). See [`ARCHITECTURE.md`](ARCHITECTURE.md).

MUI is the declarative UI layer for the [mocida](../mocida/README.md) toolkit.
A document describes a tree of mocida widgets; logic and reactivity are written
in [Copper](https://github.com/liy77/copper-lang). The two file types are:

| Ext | Analogy | Contents |
| --- | ------- | -------- |
| `.mui`  | `.jsx` | **Markup-first.** One or more `view` functions; bindings and small handlers are inline Copper expressions. No top-level statements, imports of logic only. |
| `.crm`  | `.tsx` | **Copper + Mocida.** The full Copper language (imports, types, functions, async) plus `view` blocks. This is where real apps live. |

Both compile to the **same component AST**. A `.mui` file is just a `.crm`
restricted to the view sub-language.

---

## 1. Component model

A component is a `view` function. It takes typed props (with optional
defaults) and its body is a UI tree.

```crm
view Greeting(name: string = "world", big: bool = false) {
  Text("Hi, ${name}", size: big ? 32 : 16)
}
```

- **Props** are the function parameters. Types use Copper types (`int`,
  `string`, `bool`, structs, `fn(...)`, …).
- **Calling** a view renders it: `Greeting(name: "Ada", big: true)`.
- **Body** is one or more elements. Multiple top-level elements form an
  implicit fragment (rendered into the parent's child collection).
- A view returns an opaque `View` value; you never manipulate raw widgets
  from MUI (drop to Rust/`mocida-rs` for that).

### State, effects, computed

State uses **Copper's own binding syntax** — `mut name = value` for a mutable
binding, bare `name = value` for an immutable one. **There is no `let` in
Copper** (and none in MUI).

```crm
mut count   = signal(0)              // reactive cell (mocida Signal)
doubled     = computed { count * 2 } // derived, re-reads when deps change
effect { println!("count is ${count}") }  // side effect, re-runs on change
```

`signal`, `computed`, and `effect` are the only reactivity primitives. They
map onto mocida's existing `Signal` / `bind` system — MUI does not introduce a
second reactivity model.

---

## 2. Elements

```
Widget(positional?, name: value, name2: value) { children }
Widget(name: value)                                  // no children
Widget                                               // no args, no children
```

- **Positional argument** (optional, first) is the element's *primary
  content* — e.g. a label: `Text("hi")`, `Button("Save")`.
- **Named props** map to the widget's setters (`size:` → `SetFontSize`, etc.).
- **Children block** `{ ... }` holds nested elements and control flow.
- Elements are **PascalCase**; props are **camelCase**.

### Event handlers

Handlers are Copper closures supplied as props:

```crm
Button("Save", onClick: { save() })
Slider(min: 0, max: 1, value: t, onChange: { |v| log(v) })
```

A handler with no params uses bare `{ ... }`; with params, `{ |a, b| ... }`.
At codegen they become the C-ABI trampolines `mocida-rs` already uses.

### Bindings

- **Interpolation** `"...${expr}..."` — reactive one-way text.
- **Expression prop** `prop: expr` — `expr` is Copper. If it reads a signal,
  that prop is reactively bound (re-applied on change).
- **Two-way** — input widgets (`Slider`, `TextField`, `Checkbox`, `Switch`,
  `TextArea`, `RadioButton`) treat `value:`/`checked:` bound to a **signal**
  as two-way: user input writes the signal; writing the signal updates the
  widget. Binding a non-signal expression is one-way (display only).

### Keys

`key: expr` gives an element a stable identity for reconciliation (required
inside `for`). See [§6](#6-reconciliation--hot-reload).

### Color & units

- Color literal: `#rrggbb` or `#rrggbbaa`; or `rgba(r, g, b, a)` → `UIColor`.
- Sizes/positions are numbers in logical px (floats allowed).

---

## 3. Control flow (in children position)

```crm
if cond { ... } else if other { ... } else { ... }

for item in iterable {            // requires key:
  Row(data: item, key: item.id)
}

match expr {
  some(x) => Text(x),
  none    => Spinner,
}
```

These are the Copper control-flow forms, valid wherever children are. They are
re-evaluated reactively if their condition/collection reads a signal.

---

## 4. Grammar (EBNF, markup layer)

Expressions, types, patterns, and statements are **Copper**; MUI only adds the
element/children/control-flow productions. `EXPR`, `TYPE`, `PATTERN`, `BLOCK`,
and `IDENT` are Copper nonterminals.

```ebnf
document   = { view_decl } ;                 (* .mui: only views + import *)
view_decl  = "view" IDENT "(" [ params ] ")" element_block ;
params     = param { "," param } ;
param      = IDENT ":" TYPE [ "=" EXPR ] ;

element_block = "{" { node } "}" ;
node       = element | control | bind_stmt | effect_stmt | EXPR_stmt ;

element    = IDENT [ "(" [ args ] ")" ] [ element_block ] ;
args       = arg { "," arg } ;
arg        = EXPR                              (* positional, first only *)
           | IDENT ":" arg_value ;
arg_value  = EXPR | handler ;
handler    = "{" [ "|" [ IDENT { "," IDENT } ] "|" ] STMTS "}" ;

control    = if_node | for_node | match_node ;
if_node    = "if" EXPR element_block { "else" "if" EXPR element_block }
             [ "else" element_block ] ;
for_node   = "for" PATTERN "in" EXPR element_block ;   (* children need key: *)
match_node = "match" EXPR "{" { PATTERN "=>" (element | element_block) "," } "}" ;

(* Copper binding syntax — `mut` for mutable, bare for immutable. NO `let`. *)
bind_stmt   = [ "mut" ] IDENT "=" EXPR ;       (* signal/computed/value *)
effect_stmt = "effect" BLOCK ;
```

A `.crm` is the host Copper grammar with `view_decl` added as a top-level item
and `element`/`control` allowed inside `view` bodies.

---

## 5. Reactivity semantics

- A **signal** holds a value of a `SignalValue` type (`int`, `float`, `bool`,
  `string`, or an opaque pointer — same set mocida's `Signal` supports).
- Reading a signal inside a *binding context* (interpolation, prop expr,
  `computed`, `effect`, `if`/`for`/`match` head) registers a **dependency**.
- Writing a signal (`count += 1`, `count.set(x)`, `count.update(f)`) notifies
  dependents; only the bound props / subtrees re-apply. Unrelated widgets are
  untouched (granular updates via mocida `Signal` subscriptions).
- `computed { ... }` is a lazily-cached derived signal.
- `effect { ... }` runs once on mount and again whenever a dep changes.

Update ordering and two-way loops: a signal write triggers at most one
re-apply per dependent per tick; two-way inputs guard against feedback by
not re-writing a signal with an equal value.

---

## 6. Reconciliation / hot-reload

mocida is **retained-mode**: the widget tree persists between frames. On a
hot-reload (a `.mui`/`.crm` file changed) the dev runtime re-parses, produces a
new component tree, and **diffs it against the live tree**:

- Nodes are matched by **key** (explicit `key:`) or, absent a key, by their
  structural path + element type.
- Matched nodes are **reused** (their bound signals keep their values, so app
  state survives a reload); changed props are re-applied; added/removed nodes
  are created/destroyed.
- Matching leans on mocida's `UIWidget_SetId` / `UIWidget_FindByData` /
  `walker` to locate live nodes.

`for` requires `key:` so list items reconcile correctly.

---

## 7. Initial widget mapping

The first mapped set (every `UIWidget`-backed mocida type can be exposed; this
is the starter surface). Element → mocida type; common props in parentheses.

| MUI element | mocida type | Common props |
| ----------- | ----------- | ------------ |
| `Stack`     | `UIStack`   | `orientation` (vertical\|horizontal), `gap`, `padding` |
| `Grid` / `GridView` | `UIGrid` / `UIGridView` | `columns`, `gap` |
| `ListView`  | `UIListView` | `gap` |
| `Scroll`    | `UIScroll`  | `direction` |
| `Text`      | `UIText`    | *(label)*, `size`, `color`, `align`, `wrap`, `fontStyle` |
| `Button`    | `UIButton`  | *(label)*, `size`, `fontStyle`, `colors`, `radius`, `borderWidth`, `cursor`, `onClick` |
| `Image`     | `UIImage`   | *(source)*, `fillMode`, `tint` |
| `Checkbox`  | `UICheckbox`| `value`, `onChange`, `colors` |
| `Switch`    | `UISwitch`  | `value`, `onChange` |
| `RadioButton`| `UIRadioButton` | `group`, `value`, `onChange` |
| `Slider`    | `UISlider`  | `min`, `max`, `value`, `onChange` |
| `ProgressBar`| `UIProgressBar` | `value`, `indeterminate` |
| `Spinner`   | `UISpinner` | `radius`, `color` |
| `TextField` | `UITextField` | `value`, `placeholder`, `onChange` |
| `TextArea`  | `UITextArea`| `value`, `placeholder` |
| `Dropdown` / `Menu` / `Tooltip` | `UIDropdown` / `UIMenu` / `UITooltip` | items, `onSelect` |
| `Dialog`    | `UIDialog`  | `open`, `onClose` |
| `TabView`   | `UITabView` | tabs |
| `Video`     | `UIVideo`   | `source` |
| `WebView`   | `UIWebView` | `url` |
| `MouseArea` | `UIMouseArea` | `onPress`, `onDrag` |

Enums (`FontStyle`, `FillMode`, alignment, `Ease`, …) are referenced by their
mocida names (`FontStyle.Bold`). `theme.h` values and `anim.h` easings are
available as Copper values.

---

## 8. Non-goals (v0)

- No CSS-like stylesheet language — styling is props + `theme`.
- No HTML/DOM compatibility.
- No raw-widget manipulation from MUI (use `mocida-rs` directly for that).
- The markup is not a general document format; it only describes mocida trees.

## 9. Open questions

- Slots / children-as-prop for container components.
- Scoped styling / theme overrides per subtree.
- Animation syntax sugar (transitions on prop change via `anim.h`).
- Module/namespacing for view libraries.
