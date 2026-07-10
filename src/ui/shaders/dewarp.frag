#version 440

// Fisheye -> rectilinear dewarp for 360/panoramic Reolink cameras (DESIGN §5.2).
// Casts a perspective ray per output pixel, rotates by pan/tilt, and samples the
// equidistant fisheye source. strength=0 is passthrough; 1 is full dewarp.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float pan;      // radians, yaw
    float tilt;     // radians, pitch
    float fov;      // output field of view (radians)
    float strength; // 0..1 dewarp mix
};

const float PI = 3.14159265359;

void main() {
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;      // -1..1

    // Perspective ray from the virtual camera.
    float f = 1.0 / tan(fov * 0.5);
    vec3 ray = normalize(vec3(uv.x, uv.y, f));

    // Rotate: yaw (pan) about Y, then pitch (tilt) about X.
    float cp = cos(pan), sp = sin(pan);
    vec3 r1 = vec3(cp * ray.x + sp * ray.z, ray.y, -sp * ray.x + cp * ray.z);
    float ct = cos(tilt), st = sin(tilt);
    vec3 r2 = vec3(r1.x, ct * r1.y - st * r1.z, st * r1.y + ct * r1.z);

    // Equidistant fisheye projection (180-degree lens).
    float theta = acos(clamp(r2.z, -1.0, 1.0));
    float phi = atan(r2.y, r2.x);
    float radius = theta / (PI * 0.5);
    vec2 fish = vec2(cos(phi), sin(phi)) * radius * 0.5 + 0.5;

    vec2 srcUV = mix(qt_TexCoord0, fish, strength);
    // Guard against sampling outside the fisheye circle.
    vec4 col = texture(source, clamp(srcUV, 0.0, 1.0));
    fragColor = col * qt_Opacity;
}
