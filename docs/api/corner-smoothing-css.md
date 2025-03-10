## CSS Rule: `-electron-corner-smoothing`

> Smoothes out the corner rounding of the `border-radius` CSS rule.

The rounded corners of elements with [the `border-radius` CSS rule](https://developer.mozilla.org/en-US/docs/Web/CSS/border-radius) can be smoothed out using the `-electron-corner-smoothing` CSS rule. This smoothness is very similar to Apple's "continuous" rounded corners in SwiftUI and Figma's "corner smoothing" control on design elements.

![There is a black rectangle on the left using simple rounded corners, and a blue rectangle on the right using smooth rounded corners. In between those rectangles is a magnified view of the same corner from both rectangles overlapping to show the subtle difference in shape.](../images/corner-smoothing-summary.svg)

Integrating with the operating system and its design language is important to many desktop applications. The shape of a rounded corner can be a subtle detail to many users. However, aligning closely to the system's design language that users are familiar with makes the application's design feel familiar too. Beyond matching the design language of macOS, designers may decide to use smoother round corners for many other reasons.

`-electron-corner-smoothing` affects the shape of borders, outlines, and shadows on the target element. Mirroring the behavior of `border-radius`, smoothing will gradually back off if an element's size is too small for the chosen value.

The `-electron-corner-smoothing` CSS rule is **only implemented for Electron** and has no effect in browsers. Avoid using this rule outside of Electron. This CSS rule is considered experimental and may require migration in the future if replaced by a CSS standard.

### Example

The following example shows the effect of corner smoothing at different percents.

```css
.box {
  width: 128px;
  height: 128px;
  background-color: cornflowerblue;
  border-radius: 24px;
  -electron-corner-smoothing: var(--percent);  /* Column header in table below. */
}
```

| 0% | 30% | 60% | 100% |
| --- | --- | --- | --- |
| ![A rectangle with round corners at 0% smoothness](../images/corner-smoothing-example-0.svg) | ![A rectangle with round corners at 30% smoothness](../images/corner-smoothing-example-30.svg) | ![A rectangle with round corners at 60% smoothness](../images/corner-smoothing-example-60.svg) | ![A rectangle with round corners at 100% smoothness](../images/corner-smoothing-example-100.svg) |

### Matching the system UI

Use the `system-ui` keyword to match the smoothness to the OS design language.

```css
.box {
  width: 128px;
  height: 128px;
  background-color: cornflowerblue;
  border-radius: 24px;
  -electron-corner-smoothing: system-ui;  /* Match the system UI design. */
}
```

| OS: | macOS | Windows, Linux |
| --- | --- | --- |
| Value: | `60%` | `0%` |
| Example: | ![A rectangle with round corners whose smoothness matches macOS](../images/corner-smoothing-example-60.svg) | ![A rectangle with round corners whose smoothness matches Windows and Linux](../images/corner-smoothing-example-0.svg) |

### Controlling availibility

This CSS rule can be disabled by setting [the `cornerSmoothingCSS` web preference](./structures/web-preferences.md) to `false`.

```js
const myWindow = new BrowserWindow({
  // [...]
  webPreferences: {
    enableCornerSmoothingCSS: false // Disables the `-electron-corner-smoothing` CSS rule
  }
})
```

The CSS rule will still parse, but will have no visual effect.

### Formal reference

* **Initial value**: `0%`
* **Inherited**: No
* **Animatable**: No
* **Computed value**: As specified

```css
-electron-corner-smoothing =
  <percentage [0,100]>  |
  system-ui
```
