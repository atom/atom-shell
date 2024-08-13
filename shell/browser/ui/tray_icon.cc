// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/tray_icon.h"

namespace electron {

TrayIcon::BalloonOptions::BalloonOptions() = default;

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() = default;

gfx::Rect TrayIcon::GetBounds() {
  return gfx::Rect();
}

void TrayIcon::NotifyClicked(const gfx::Rect& bounds,
                             const gfx::Point& location,
                             int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnClicked(bounds, location, modifiers);
}

void TrayIcon::NotifyDoubleClicked(const gfx::Rect& bounds, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnDoubleClicked(bounds, modifiers);
}

void TrayIcon::NotifyMiddleClicked(const gfx::Rect& bounds, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnMiddleClicked(bounds, modifiers);
}

void TrayIcon::NotifyBalloonShow() {
  for (TrayIconObserver& observer : observers_)
    observer.OnBalloonShow();
}

void TrayIcon::NotifyBalloonClicked() {
  for (TrayIconObserver& observer : observers_)
    observer.OnBalloonClicked();
}

void TrayIcon::NotifyBalloonClosed() {
  for (TrayIconObserver& observer : observers_)
    observer.OnBalloonClosed();
}

void TrayIcon::NotifyRightClicked(const gfx::Rect& bounds, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnRightClicked(bounds, modifiers);
}

void TrayIcon::NotifyDrop() {
  for (TrayIconObserver& observer : observers_)
    observer.OnDrop();
}

void TrayIcon::NotifyDropFiles(const std::vector<std::string>& files) {
  for (TrayIconObserver& observer : observers_)
    observer.OnDropFiles(files);
}

void TrayIcon::NotifyDropText(const std::string& text) {
  for (TrayIconObserver& observer : observers_)
    observer.OnDropText(text);
}

void TrayIcon::NotifyMouseUp(const gfx::Point& location, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnMouseUp(location, modifiers);
}

void TrayIcon::NotifyMouseDown(const gfx::Point& location, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnMouseDown(location, modifiers);
}

void TrayIcon::NotifyMouseEntered(const gfx::Point& location, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnMouseEntered(location, modifiers);
}

void TrayIcon::NotifyMouseExited(const gfx::Point& location, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnMouseExited(location, modifiers);
}

void TrayIcon::NotifyMouseMoved(const gfx::Point& location, int modifiers) {
  for (TrayIconObserver& observer : observers_)
    observer.OnMouseMoved(location, modifiers);
}

void TrayIcon::NotifyDragEntered() {
  for (TrayIconObserver& observer : observers_)
    observer.OnDragEntered();
}

void TrayIcon::NotifyDragExited() {
  for (TrayIconObserver& observer : observers_)
    observer.OnDragExited();
}

void TrayIcon::NotifyDragEnded() {
  for (TrayIconObserver& observer : observers_)
    observer.OnDragEnded();
}

}  // namespace electron
