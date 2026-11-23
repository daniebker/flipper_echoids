package com.echoids.app;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.View;

/** Shows the game's 128x64 screen, scaled up with crisp pixels in Flipper orange. */
final class ScreenView extends View {
    static final int W = 128, H = 64;
    private static final int ORANGE = 0xFFFF8200, BLACK = 0xFF000000;

    private final Bitmap bitmap = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
    private final int[] argb = new int[W * H];
    private final Paint paint = new Paint();
    private final Paint frame = new Paint();
    private final Rect dst = new Rect();

    ScreenView(Context context) {
        super(context);
        paint.setFilterBitmap(false);
        paint.setAntiAlias(false);
        frame.setColor(0xFF2A2A2A);
        bitmap.eraseColor(ORANGE);
    }

    /** pixels: 128 * 64 bytes, 1 = black. */
    void setPixels(byte[] pixels) {
        for(int i = 0; i < argb.length; i++) argb[i] = pixels[i] != 0 ? BLACK : ORANGE;
        bitmap.setPixels(argb, 0, W, 0, 0, W, H);
        invalidate();
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int width = MeasureSpec.getSize(widthSpec);
        setMeasuredDimension(width, width / 2 + width / 12);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        canvas.drawColor(Color.rgb(20, 20, 20));
        int pad = getWidth() / 24;
        int availW = getWidth() - 2 * pad, availH = getHeight() - 2 * pad;
        /* Largest whole-number scale that fits, so every Flipper pixel is the same size. */
        int scale = Math.max(1, Math.min(availW / W, availH / H));
        int w = W * scale, h = H * scale;
        int left = (getWidth() - w) / 2, top = (getHeight() - h) / 2;
        canvas.drawRect(left - pad / 2f, top - pad / 2f, left + w + pad / 2f, top + h + pad / 2f, frame);
        dst.set(left, top, left + w, top + h);
        canvas.drawBitmap(bitmap, null, dst, paint);
    }
}
