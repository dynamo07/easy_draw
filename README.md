# Easy Draw
A shortcut-only on-screen annotation software for Windows inspired by gInk.

<img width="950" height="285" alt="{3E360FB7-76B6-480A-881F-2CBC63560104}" src="https://github.com/user-attachments/assets/cdc34d78-20fb-4a25-a903-a91a7cd54bcf" />

## Compile (MinGW-w64):
g++ easy_draw.cpp -o Easy_Draw.exe -pipe -flto -mwindows -municode -Wl,--stack,12582912 -s -ld3d11 -ldxgi -ld2d1 -ldwrite -ldcomp -lole32 -luuid -lshell32 -lgdi32 -ldxguid -mwindows -static

## Features:
Pressing **Ctrl + 2** can enable all features, allowing you to **hold the left mouse button** to draw freehand lines on the screen. 
However, you cannot operate on the underlying window unless you press **Ctrl + 2** again to disable it. What you have drawn will be kept on the screen.

**Scrolling the mouse wheel** can adjust the line width, with a circle previewing the thickness.

Pressing a color key (**R, G, B, Y, C, O, P, K, W, V**) changes the color to red, green, blue, yellow, cyan, orange, pink, black, white, or violet.
To apply a highlighter style to a color, press **Ctrl + the corresponding color key** (e.g., **Ctrl + R**).

**Clicking the right mouse button** enables you to type text at the cursor's location. A vertical line indicates the text size, which you can adjust with the mouse wheel. To exit text mode, click the left mouse button or hold it down to draw.

Pressing **E** toggles the erase mode. Hold the left mouse button to erase drawings. A square indicates the eraser size, which you can change by scrolling the mouse wheel. To exit the erase mode, press a color key or right-click.

**Ctrl + Z** is undo; **Ctrl + A** is redo.

Pressing **D** clears all.

To exit the program, click the program's icon in the system tray and select **Exit** from the pop-up menu. The **Open config.txt** option in the menu allows you to customize the shortcuts.
