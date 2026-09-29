package app.mmfpocket.player;

import java.util.ArrayList;
import java.util.Arrays;

/** Small opaque, lit 3D mesh. No sprite switching, alpha dissolves or frame history. */
final class MafuMesh {
    private static final int OUTLINE = 0xff263238;
    private static final int OUTLINE_RADIUS = 5;
    private static final int[] OUTLINE_HALF_WIDTH = {5, 4, 4, 4, 3, 0};
    private static final int WHITE = 0xfcfeff;
    private static final int BLUE = 0x91d2fa;
    private static final int INK = 0x394f60;
    private final ArrayList<float[]> building = new ArrayList<>();
    private final float[][] vertices;
    private final float[][] projected;
    private final int size;
    final int[] pixels;
    private final int[] surfacePixels;
    private final float[] depth;

    MafuMesh(int size) {
        this.size = size;
        pixels = new int[size * size];
        surfacePixels = new int[pixels.length];
        depth = new float[pixels.length];
        roundedBox(0, 0, 0, 16, 15, 7, 7f, WHITE);
        // Small rounded chip contacts, tucked against the body.
        for (int side : new int[] {-1, 1}) {
            for (int row = -1; row <= 1; row++) {
                roundedBox(side * 16.5f, row * 4.5f - 1, 0,
                        2, 1.5f, 2.5f, 1.1f, 0xb8dcf2);
            }
            ellipsoid(side * 14, 10, 4, 3.7f, 4, 3.3f, WHITE);
            ellipsoid(side * 7, 16.7f, 0.8f, 4, 2.5f, 4, WHITE);
            // The ears are actual solid musical notes, including a stem and flag.
            note(side * 10, -17, 0, BLUE);
            ellipsoid(side * 5.5f, -1, 7.05f, 1.35f, 2, 0.55f, INK);
            ellipsoid(side * 9, 3, 6.95f, 2.3f, 1.2f, 0.35f, 0xb5ddf6);
            ellipsoid(side * 5.7f, -1.7f, 7.55f, 0.35f, 0.45f, 0.15f, WHITE);
        }
        for (int i = 0; i <= 12; i++) {
            float x = -2.2f + i * 4.4f / 12;
            float y = 3 + 1.3f * (1 - x * x / (2.2f * 2.2f));
            ellipsoid(x, y, 7.15f, 0.42f, 0.42f, 0.32f, INK);
        }
        // A raised blue music emblem identifies the back during a flip.
        ellipsoid(-1.7f, 3, -7.15f, 2, 1.3f, 0.45f, BLUE);
        roundedBox(0, 0, -7.15f, 0.6f, 3, 0.35f, 0.25f, BLUE);
        ellipsoid(1.5f, -2.5f, -7.15f, 2, 0.8f, 0.35f, BLUE);
        vertices = building.toArray(new float[0][]);
        building.clear();
        projected = new float[vertices.length][6];
    }

    private void note(float x, float y, float z, int color) {
        ellipsoid(x - 1, y, z, 3.2f, 2.4f, 2, color);
        roundedBox(x + 1.2f, y - 3.5f, z, 0.9f, 4, 1.2f, 0.8f, color);
        ellipsoid(x + 3, y - 6, z, 2.7f, 1.5f, 1.3f, color);
    }

    private void ellipsoid(float cx, float cy, float cz, float rx, float ry,
            float rz, int color) {
        int rings = 8, slices = 12;
        for (int j = 0; j < rings; j++) {
            for (int i = 0; i < slices; i++) {
                float[] a = spherePoint(cx, cy, cz, rx, ry, rz, j, i, rings, slices, color);
                float[] b = spherePoint(cx, cy, cz, rx, ry, rz, j + 1, i, rings, slices, color);
                float[] c = spherePoint(cx, cy, cz, rx, ry, rz, j + 1, i + 1, rings, slices, color);
                float[] d = spherePoint(cx, cy, cz, rx, ry, rz, j, i + 1, rings, slices, color);
                quad(a, b, c, d);
            }
        }
    }

    private static float[] spherePoint(float cx, float cy, float cz, float rx,
            float ry, float rz, int j, int i, int rings, int slices, int color) {
        double phi = Math.PI * j / rings, theta = Math.PI * 2 * i / slices;
        float x = (float) (Math.sin(phi) * Math.cos(theta));
        float y = (float) Math.cos(phi);
        float z = (float) (Math.sin(phi) * Math.sin(theta));
        return vertex(cx + rx * x, cy + ry * y, cz + rz * z,
                x / rx, y / ry, z / rz, color);
    }

    private void roundedBox(float cx, float cy, float cz, float hx, float hy,
            float hz, float radius, int color) {
        // Sample each face of a box and round its edges onto a sphere of radius r.
        int steps = color == WHITE && hx == 16 ? 16 : 4;
        float[] half = {hx, hy, hz};
        for (int axis = 0; axis < 3; axis++) {
            for (int sign : new int[] {-1, 1}) {
                for (int j = 0; j < steps; j++) {
                    for (int i = 0; i < steps; i++) {
                        quad(boxPoint(cx, cy, cz, half, radius, axis, sign, i, j, steps, color),
                                boxPoint(cx, cy, cz, half, radius, axis, sign, i + 1, j, steps, color),
                                boxPoint(cx, cy, cz, half, radius, axis, sign, i + 1, j + 1, steps, color),
                                boxPoint(cx, cy, cz, half, radius, axis, sign, i, j + 1, steps, color));
                    }
                }
            }
        }
    }

    private static float[] boxPoint(float cx, float cy, float cz, float[] half,
            float r, int axis, int sign, int i, int j, int steps, int color) {
        float[] p = new float[3], core = new float[3], normal = new float[3];
        p[axis] = sign * half[axis];
        p[(axis + 1) % 3] = (2f * i / steps - 1) * half[(axis + 1) % 3];
        p[(axis + 2) % 3] = (2f * j / steps - 1) * half[(axis + 2) % 3];
        float length = 0;
        for (int k = 0; k < 3; k++) {
            core[k] = Math.max(-half[k] + r, Math.min(half[k] - r, p[k]));
            normal[k] = p[k] - core[k];
            length += normal[k] * normal[k];
        }
        length = (float) Math.sqrt(length);
        for (int k = 0; k < 3; k++) {
            normal[k] /= length;
            p[k] = core[k] + r * normal[k];
        }
        return vertex(cx + p[0], cy + p[1], cz + p[2],
                normal[0], normal[1], normal[2], color);
    }

    private static float[] vertex(float x, float y, float z, float nx, float ny,
            float nz, int color) {
        float length = (float) Math.sqrt(nx * nx + ny * ny + nz * nz);
        return new float[] {x, y, z, nx / length, ny / length, nz / length,
                (color >> 16) & 255, (color >> 8) & 255, color & 255};
    }

    private void quad(float[] a, float[] b, float[] c, float[] d) {
        building.add(a); building.add(b); building.add(c);
        building.add(a); building.add(c); building.add(d);
    }

    void render(float pitchDegrees, float yawDegrees) {
        Arrays.fill(pixels, 0);
        Arrays.fill(surfacePixels, 0);
        Arrays.fill(depth, Float.NEGATIVE_INFINITY);
        double pitch = Math.toRadians(pitchDegrees), yaw = Math.toRadians(yawDegrees);
        float sx = (float) Math.sin(pitch), cx = (float) Math.cos(pitch);
        float sy = (float) Math.sin(yaw), cy = (float) Math.cos(yaw);
        float scale = size / 64f;
        for (int i = 0; i < vertices.length; i++) {
            float[] v = vertices[i], p = projected[i];
            float y = cx * v[1] - sx * v[2], z = sx * v[1] + cx * v[2];
            float x = cy * v[0] + sy * z;
            z = -sy * v[0] + cy * z;
            p[0] = size * 0.5f + x * scale;
            p[1] = size * 0.5f + y * scale;
            p[2] = z;
            float ny = cx * v[4] - sx * v[5], nz = sx * v[4] + cx * v[5];
            float nx = cy * v[3] + sy * nz;
            nz = -sy * v[3] + cy * nz;
            float diffuse = Math.max(0, -0.35f * nx - 0.55f * ny + 0.76f * nz);
            float shine = Math.max(0, -0.2f * nx - 0.3f * ny + 0.933f * nz);
            shine = shine * shine * shine * shine;
            shine = shine * shine * shine;
            if (v[6] + v[7] + v[8] > 700f) {
                // White plush material: unlit areas become pale blue, never gray.
                float softness = 0.20f + 0.80f * diffuse;
                p[3] = Math.min(255, 212 + (v[6] - 212) * softness + 8 * shine);
                p[4] = Math.min(255, 236 + (v[7] - 236) * softness + 8 * shine);
                p[5] = Math.min(255, 251 + (v[8] - 251) * softness + 8 * shine);
            } else {
                float light = 0.88f + 0.12f * diffuse;
                p[3] = Math.min(255, v[6] * light + 8 * shine);
                p[4] = Math.min(255, v[7] * light + 8 * shine);
                p[5] = Math.min(255, v[8] * light + 8 * shine);
            }
        }
        for (int i = 0; i < projected.length; i += 3) {
            triangle(projected[i], projected[i + 1], projected[i + 2]);
        }
        addOutline();
    }

    private void triangle(float[] a, float[] b, float[] c) {
        float area = (b[1] - c[1]) * (a[0] - c[0])
                + (c[0] - b[0]) * (a[1] - c[1]);
        if (Math.abs(area) < 0.0001f) return;
        int left = Math.max(0, (int) Math.floor(Math.min(a[0], Math.min(b[0], c[0]))));
        int right = Math.min(size - 1, (int) Math.ceil(Math.max(a[0], Math.max(b[0], c[0]))));
        int top = Math.max(0, (int) Math.floor(Math.min(a[1], Math.min(b[1], c[1]))));
        int bottom = Math.min(size - 1, (int) Math.ceil(Math.max(a[1], Math.max(b[1], c[1]))));
        for (int y = top; y <= bottom; y++) {
            for (int x = left; x <= right; x++) {
                float wa = ((b[1] - c[1]) * (x + 0.5f - c[0])
                        + (c[0] - b[0]) * (y + 0.5f - c[1])) / area;
                float wb = ((c[1] - a[1]) * (x + 0.5f - c[0])
                        + (a[0] - c[0]) * (y + 0.5f - c[1])) / area;
                float wc = 1 - wa - wb;
                if (wa < -0.00001f || wb < -0.00001f || wc < -0.00001f) continue;
                float z = wa * a[2] + wb * b[2] + wc * c[2];
                int index = y * size + x;
                if (z <= depth[index]) continue;
                depth[index] = z;
                int red = (int) (wa * a[3] + wb * b[3] + wc * c[3]);
                int green = (int) (wa * a[4] + wb * b[4] + wc * c[4]);
                int blue = (int) (wa * a[5] + wb * b[5] + wc * c[5]);
                surfacePixels[index] = 0xff000000 | (red << 16) | (green << 8) | blue;
            }
        }
    }

    private void addOutline() {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int source = y * size + x;
                if (surfacePixels[source] == 0) continue;
                for (int dy = -OUTLINE_RADIUS; dy <= OUTLINE_RADIUS; dy++) {
                    int targetY = y + dy;
                    if (targetY < 0 || targetY >= size) continue;
                    int maxDx = OUTLINE_HALF_WIDTH[Math.abs(dy)];
                    int startX = Math.max(0, x - maxDx);
                    int endX = Math.min(size - 1, x + maxDx);
                    int row = targetY * size;
                    for (int targetX = startX; targetX <= endX; targetX++) {
                        int target = row + targetX;
                        if (surfacePixels[target] == 0) pixels[target] = OUTLINE;
                    }
                }
            }
        }
        for (int i = 0; i < pixels.length; i++) {
            if (surfacePixels[i] != 0) pixels[i] = surfacePixels[i];
        }
    }
}
