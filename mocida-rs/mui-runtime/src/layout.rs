//! A trivial top-down layout cursor for the static runtime.
//!
//! mocida widgets are absolutely positioned unless they live inside a
//! container (`Stack`, `Grid`, …) that lays them out. For widgets we place
//! directly (e.g. `Text` at the top level, before real container support is
//! wired for every element), this hands out increasing y-offsets so they don't
//! overlap. Inside a `Stack` the container owns layout, so each `Stack` builds
//! its children against a fresh root cursor and the offsets are ignored.

/// Content origin, in logical px. Zero so the root is flush with the window —
/// `padding: 0` on the root really means no spacing (an implicit margin here was
/// surprising). Add `padding:` to a widget for inset spacing.
const MARGIN_X: f32 = 0.0;
const MARGIN_TOP: f32 = 0.0;
/// Vertical gap added after each placed widget.
pub const ROW_GAP: f32 = 8.0;

/// A simple vertical cursor: each [`Layout::next`] returns the next `(x, y)`
/// and advances `y` past the placed widget. It also tracks the widest row, so a
/// container can size itself to its content.
pub struct Layout {
    x: f32,
    y: f32,
    start_y: f32,
    max_w: f32,
}

impl Layout {
    /// A fresh cursor at the top-left content origin.
    pub fn root() -> Self {
        Layout {
            x: MARGIN_X,
            y: MARGIN_TOP,
            start_y: MARGIN_TOP,
            max_w: 0.0,
        }
    }

    /// Return the position for the next widget and advance the cursor by
    /// `height + ROW_GAP`. Pass the widget's width so the cursor can track the
    /// content's overall width too (use [`Layout::next`] when width is unknown).
    pub fn next_sized(&mut self, width: f32, height: f32) -> (f32, f32) {
        let pos = (self.x, self.y);
        self.y += height + ROW_GAP;
        if width > self.max_w {
            self.max_w = width;
        }
        pos
    }

    /// Total content height placed so far (distance from the start origin).
    pub fn content_height(&self) -> f32 {
        (self.y - self.start_y).max(0.0)
    }

    /// Widest row placed so far.
    pub fn content_width(&self) -> f32 {
        self.max_w
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn consecutive_widgets_get_distinct_increasing_y() {
        // Guards the overlap bug: each placed widget must sit below the last.
        let mut l = Layout::root();
        let (_, y0) = l.next_sized(100.0, 30.0);
        let (_, y1) = l.next_sized(100.0, 20.0);
        let (_, y2) = l.next_sized(100.0, 20.0);
        assert!(
            y1 > y0,
            "second widget must be below the first ({y1} > {y0})"
        );
        assert!(
            y2 > y1,
            "third widget must be below the second ({y2} > {y1})"
        );
        assert_eq!(y1 - y0, 30.0 + ROW_GAP, "advance = height + gap");
    }
}
