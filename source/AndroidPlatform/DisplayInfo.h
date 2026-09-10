#pragma once

// ---------------------------------------------------------------------------
// DisplayInfo: host display dimensions.
//
// Resolve the Activity through ActivityHost and use reflection to
//   activity.getSystemService(WINDOW_SERVICE).getDefaultDisplay().getRealSize(point)
// read physical pixels; normalize to landscape and cache for process lifetime.
// ---------------------------------------------------------------------------
namespace AndroidPlatform
{

struct DisplaySize
{
    int width;   // Physical pixels in landscape orientation.
    int height;
};

// Return host display dimensions normalized to landscape.
// Return {0, 0} when the Activity is unavailable or JNI fails.
DisplaySize GetDisplaySize();

}
