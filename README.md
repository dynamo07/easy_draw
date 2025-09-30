# Easy Draw
A shortcut-only on-screen annotation software for Windows, inspired by gInk.
It is designed to assist teaching.

<img width="950" height="285" alt="{3E360FB7-76B6-480A-881F-2CBC63560104}" src="https://github.com/user-attachments/assets/cdc34d78-20fb-4a25-a903-a91a7cd54bcf" />

## Compile (MinGW-w64):
g++ easy_draw.cpp -o Easy_Draw.exe -mwindows -municode -Wl,--stack,12582912 -s -ld3d11 -ldxgi -ld2d1 -ldwrite -ldcomp -lole32 -luuid -lshell32 -lgdi32 -ldxguid -mwindows -static

[Download the precompiled Easy Draw executable (Windows 11)](https://github.com/dynamo07/easy_draw/releases/download/1.3.0/Easy_Draw.exe)

## Features (freehand line, text, highlighter, eraser, screenshot, and magnifier):
Pressing **Ctrl + 2** enables all features, allowing you to **hold the left mouse button** to draw freehand lines on the screen. 
However, you cannot operate on the underlying window unless you press **Ctrl + 2** again to disable it. What you have drawn will be kept on the screen.
It is also optimized for drawing on the touch screen with one finger.

**Scrolling the mouse wheel** can adjust the line width, which is indicated by a circle.

Pressing a color key (**R, G, B, Y, C, O, P, K, W, V**) changes the color to red, green, blue, yellow, cyan, orange, pink, black, white, or violet.
To apply a highlighter style to a color, press **Ctrl + the corresponding color key** (e.g., **Ctrl + R**).

**Clicking the right mouse button** enables you to type text at the cursor's location. A vertical line indicates the text size, which you can adjust with the mouse wheel. To exit text mode, click the left mouse button or hold it down to draw.

Pressing **E** toggles the eraser mode. Hold the left mouse button to erase drawings. A square indicates the eraser size, which you can change by scrolling the mouse wheel. To exit the eraser mode, press **E**, a color key, or right-click.

**Ctrl + Z** is undo; **Ctrl + A** is redo.

Pressing **D** clears all.

Pressing **S** to take a screenshot and save it to the Pictures folder by default.

Pressing **M** toggles the magnifier mode. In this mode, you can hold the left mouse button to drag a rectangle, which acts as a magnifier, allowing you to enlarge the content under your cursor. Scrolling the mouse wheel can adjust the zoom level. To exit the magnifier mode, press **M** again.

To exit the program, click the program's icon in the system tray and select **Exit** from the pop-up menu. The **Open config.txt** option in the menu allows you to customize the shortcuts. An extended screen is supported.
