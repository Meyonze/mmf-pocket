package app.mmfpocket.player;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.View;

/** A single solid mascot mesh, animated on display frames. */
final class IllustratedMafuMascotView extends View {
    static final int PACE_SLOW = 0;
    static final int PACE_NORMAL = 1;
    static final int PACE_FAST = 2;

    private static final float TAU = (float) (Math.PI * 2.0);

    private static final int RENDER_SIZE = 192;
    private final MafuMesh mesh = new MafuMesh(RENDER_SIZE);
    private final Bitmap modelBitmap = Bitmap.createBitmap(
            RENDER_SIZE, RENDER_SIZE, Bitmap.Config.ARGB_8888);
    private final Paint modelPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF modelBounds = new RectF(-32, -32, 32, 32);
    private float renderedPitch = Float.NaN;
    private float renderedYaw = Float.NaN;
    private final Paint accentPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean playing;
    private int pace = PACE_NORMAL;
    private int routineVariant;
    private long stateStartedMs = SystemClock.uptimeMillis();

    IllustratedMafuMascotView(Context context) {
        super(context);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
    }

    void setPlaybackState(boolean isPlaying, int motionPace, int routineSeed) {
        int clampedPace = Math.max(PACE_SLOW, Math.min(PACE_FAST, motionPace));
        int nextRoutineVariant = Math.floorMod(routineSeed, 4);
        if (playing == isPlaying && pace == clampedPace
                && routineVariant == nextRoutineVariant) return;
        playing = isPlaying;
        pace = clampedPace;
        routineVariant = nextRoutineVariant;
        stateStartedMs = SystemClock.uptimeMillis();
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        float seconds = (SystemClock.uptimeMillis() - stateStartedMs) / 1000f;
        Motion motion = calculateMotion(seconds);
        // Leave room for the jump arc while keeping the resting model prominent.
        float unit = Math.min(getWidth(), getHeight()) / 80f;

        canvas.save();
        canvas.translate(getWidth() * 0.5f, getHeight() * 0.5f);
        canvas.scale(unit, unit);
        drawShadow(canvas, motion);
        if (motion.speedLines > 0.05f) {
            drawSpeedLines(canvas, seconds, motion.speedLines);
        }

        canvas.translate(motion.x, motion.y);
        canvas.rotate(motion.rotation);
        canvas.scale(motion.scaleX, motion.scaleY);
        drawRotatingMascot(canvas, motion);
        canvas.restore();

        if (isShown() && getWindowVisibility() == VISIBLE) {
            if (playing) postInvalidateOnAnimation();
            else postInvalidateDelayed(33L);
        }
    }

    private void drawRotatingMascot(Canvas canvas, Motion motion) {
        if (motion.flipXDegrees != renderedPitch || motion.turnYDegrees != renderedYaw) {
            mesh.render(motion.flipXDegrees, motion.turnYDegrees);
            modelBitmap.setPixels(mesh.pixels, 0, RENDER_SIZE, 0, 0, RENDER_SIZE, RENDER_SIZE);
            renderedPitch = motion.flipXDegrees;
            renderedYaw = motion.turnYDegrees;
        }
        canvas.drawBitmap(modelBitmap, null, modelBounds, modelPaint);
    }

    private Motion calculateMotion(float seconds) {
        Motion result = new Motion();
        if (!playing) {
            float breath = wave(seconds, 0.22f);
            result.y = breath * 0.35f;
            result.scaleX = 1f + breath * 0.008f;
            result.scaleY = 1f - breath * 0.008f;
            return result;
        }

        if (pace == PACE_SLOW) {
            float drift = wave(seconds, 0.34f);
            float breathe = wave(seconds, 0.72f);
            result.x = drift * 0.7f;
            result.y = wave(seconds, 0.58f) * 1.7f - 0.8f;
            result.rotation = drift * 2.4f;
            result.scaleX = 1f + breathe * 0.014f;
            result.scaleY = 1f - breathe * 0.012f;
            return applyChoreography(result, seconds, PACE_SLOW);
        }

        if (pace == PACE_NORMAL) {
            float beat = wave(seconds, 1.65f);
            float bounce = Math.abs(beat);
            float sway = wave(seconds, 0.31f);
            float flourish = smoothPulse(seconds, 7.2f, 0.72f);
            result.x = sway * (0.6f + flourish * 0.6f);
            result.y = -bounce * (2f + flourish * 1.4f);
            result.rotation = sway * 2.8f + flourish * wave(seconds, 0.82f) * 3f;
            result.scaleX = 1f + bounce * 0.025f;
            result.scaleY = 1f - bounce * 0.025f;
            return applyChoreography(result, seconds, PACE_NORMAL);
        }

        return calculateFastMotion(seconds);
    }

    private Motion calculateFastMotion(float seconds) {
        float cycle = seconds % 20f;
        Motion running = runningMotion(seconds);
        Motion hopping = hoppingMotion(seconds);
        Motion stepping = sideStepMotion(seconds);

        Motion result;
        if (cycle < 5f) result = running;
        else if (cycle < 6f) {
            result = blend(running, hopping, smooth01(cycle - 5f));
        } else if (cycle < 10f) result = hopping;
        else if (cycle < 11f) {
            result = blend(hopping, stepping, smooth01(cycle - 10f));
        } else if (cycle < 14f) result = stepping;
        else if (cycle < 15f) {
            result = blend(stepping, running, smooth01(cycle - 14f));
        } else result = running;
        return applyChoreography(result, seconds, PACE_FAST);
    }

    private Motion runningMotion(float seconds) {
        Motion result = new Motion();
        float step = wave(seconds, 2.15f);
        float stride = wave(seconds, 1.075f);
        result.x = wave(seconds, 0.54f) * 0.75f;
        result.y = -Math.abs(step) * 2.8f;
        result.rotation = -2.4f + stride * 1.8f;
        result.scaleX = 1f + Math.abs(step) * 0.022f;
        result.scaleY = 1f - Math.abs(step) * 0.025f;
        result.speedLines = 1f;
        return result;
    }

    private Motion hoppingMotion(float seconds) {
        Motion result = new Motion();
        float hop = Math.abs(wave(seconds, 1.08f));
        result.x = wave(seconds, 0.54f) * 1.1f;
        result.y = -hop * 4.4f;
        result.rotation = wave(seconds, 0.54f) * 3.2f;
        result.scaleX = 1f + hop * 0.03f;
        result.scaleY = 1f - hop * 0.022f;
        result.speedLines = 0.25f;
        return result;
    }

    private Motion sideStepMotion(float seconds) {
        Motion result = new Motion();
        float side = wave(seconds, 1.12f);
        result.x = side * 2.6f;
        result.y = -Math.abs(wave(seconds, 2.24f)) * 1.3f;
        result.rotation = side * 5f;
        result.scaleX = 1f + Math.abs(side) * 0.018f;
        result.scaleY = 1f - Math.abs(side) * 0.012f;
        return result;
    }

    private Motion applyChoreography(Motion base, float seconds, int motionPace) {
        float cycle = (seconds + routineVariant * 4f) % 16f;
        float durationScale = motionPace == PACE_SLOW ? 1.12f
                : motionPace == PACE_FAST ? 0.92f : 1f;
        float progress = actionProgress(cycle, 1.5f, 2.1f * durationScale);
        if (progress >= 0f) applyCartwheel(base, progress);
        progress = actionProgress(cycle, 5.3f, 2.8f * durationScale);
        if (progress >= 0f) applyBackflip(base, progress);
        progress = actionProgress(cycle, 9.5f, 3.2f * durationScale);
        if (progress >= 0f) applyPirouette(base, progress);
        progress = actionProgress(cycle, 13.5f, 1.8f * durationScale);
        if (progress >= 0f) applyHappyHop(base, progress);
        return base;
    }

    private void applyCartwheel(Motion motion, float progress) {
        float eased = smoother01(progress);
        float arch = (float) Math.sin(Math.PI * progress);
        motion.x += arch * 4f;
        motion.y -= arch * 1.8f;
        motion.rotation += 360f * eased;
        motion.scaleX *= 1f + arch * 0.035f;
        motion.scaleY *= 1f - arch * 0.02f;
        motion.speedLines = Math.max(motion.speedLines, arch * 0.25f);
    }

    private void applyBackflip(Motion motion, float progress) {
        float crouch;
        if (progress < 0.18f) {
            crouch = (float) Math.sin(Math.PI * progress / 0.18f);
        } else if (progress > 0.82f) {
            crouch = (float) Math.sin(Math.PI * (progress - 0.82f) / 0.18f);
        } else {
            crouch = 0f;
        }
        float flight = Math.max(0f, Math.min(1f, (progress - 0.14f) / 0.72f));
        float arch = (float) Math.sin(Math.PI * flight);
        motion.y += crouch * 1.5f - arch * 7.5f;
        motion.flipXDegrees += 360f * flight;
        motion.scaleX *= 1f + crouch * 0.08f - arch * 0.02f;
        motion.scaleY *= 1f - crouch * 0.12f + arch * 0.04f;
    }

    private void applyPirouette(Motion motion, float progress) {
        float eased = smoother01(progress);
        float arch = (float) Math.sin(Math.PI * progress);
        motion.x += (float) Math.sin(progress * Math.PI * 4f) * 0.45f;
        motion.y -= arch * 1.8f;
        motion.turnYDegrees += 360f * eased;
        motion.scaleX *= 1f - arch * 0.12f;
        motion.scaleY *= 1f + arch * 0.07f;
        motion.speedLines *= 1f - arch * 0.8f;
    }

    private void applyHappyHop(Motion motion, float progress) {
        float firstHop = (float) Math.sin(Math.PI * Math.min(1f, progress * 2f));
        float secondHop = progress < 0.5f ? 0f
                : (float) Math.sin(Math.PI * (progress - 0.5f) * 2f);
        float lift = Math.max(0f, firstHop) + Math.max(0f, secondHop) * 0.7f;
        motion.y -= lift * 2.8f;
        motion.rotation += wave(progress, 1f) * 4f;
        motion.scaleX *= 1f + lift * 0.025f;
        motion.scaleY *= 1f - lift * 0.02f;
    }

    private Motion blend(Motion from, Motion to, float amount) {
        Motion result = new Motion();
        result.x = lerp(from.x, to.x, amount);
        result.y = lerp(from.y, to.y, amount);
        result.rotation = lerp(from.rotation, to.rotation, amount);
        result.flipXDegrees = lerp(from.flipXDegrees, to.flipXDegrees, amount);
        result.turnYDegrees = lerp(from.turnYDegrees, to.turnYDegrees, amount);
        result.scaleX = lerp(from.scaleX, to.scaleX, amount);
        result.scaleY = lerp(from.scaleY, to.scaleY, amount);
        result.speedLines = lerp(from.speedLines, to.speedLines, amount);
        return result;
    }

    private void drawShadow(Canvas canvas, Motion motion) {
        accentPaint.setColor(Color.rgb(216, 229, 239));
        accentPaint.setAlpha(playing ? 150 : 115);
        float lift = Math.min(5f, Math.max(0f, -motion.y));
        float width = 18f - lift * 0.7f;
        canvas.drawOval(new RectF(-width, 27f, width, 31f), accentPaint);
    }

    private void drawSpeedLines(Canvas canvas, float seconds, float strength) {
        accentPaint.setColor(Color.rgb(126, 193, 235));
        accentPaint.setStrokeWidth(1.7f);
        accentPaint.setStrokeCap(Paint.Cap.ROUND);

        float phase = (seconds * 2.7f) % 1f;
        accentPaint.setAlpha((int) (Math.sin(Math.PI * phase) * 90f * strength));
        float shift = phase * 9f;
        canvas.drawLine(-35f + shift, -4f, -28f + shift, -4f, accentPaint);

        float secondPhase = (phase + 0.46f) % 1f;
        accentPaint.setAlpha((int) (
                Math.sin(Math.PI * secondPhase) * 72f * strength));
        float secondShift = secondPhase * 8f;
        canvas.drawLine(-34f + secondShift, 6f, -27f + secondShift, 6f, accentPaint);
        accentPaint.setAlpha(255);
    }

    private static float wave(float seconds, float hertz) {
        return (float) Math.sin(seconds * hertz * TAU);
    }

    private static float smooth01(float value) {
        float clamped = Math.max(0f, Math.min(1f, value));
        return clamped * clamped * (3f - 2f * clamped);
    }

    private static float smoother01(float value) {
        float clamped = Math.max(0f, Math.min(1f, value));
        return clamped * clamped * clamped
                * (clamped * (clamped * 6f - 15f) + 10f);
    }

    private static float actionProgress(float cycle, float start, float duration) {
        if (cycle < start || cycle > start + duration) return -1f;
        return (cycle - start) / duration;
    }

    private static float lerp(float from, float to, float amount) {
        return from + (to - from) * amount;
    }

    private static float smoothPulse(float seconds, float period, float width) {
        float position = (seconds % period) / period;
        float centerDistance = Math.abs(position - 0.5f) * 2f;
        float edge = Math.max(0f, 1f - centerDistance / width);
        return edge * edge * (3f - 2f * edge);
    }

    private static final class Motion {
        float x;
        float y;
        float rotation;
        float flipXDegrees;
        float turnYDegrees;
        float scaleX = 1f;
        float scaleY = 1f;
        float speedLines;
    }
}
