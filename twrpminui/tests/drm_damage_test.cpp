// TWRP: exercise the actual DRM damage helpers without linking/using a DRM device.
#include <cassert>
#include "../graphics_drm.cpp"

int main() {
  auto source = GRSurface::Create(3, 3, 20, 4);
  auto destination = GRSurface::Create(3, 3, 16, 4);
  assert(source && destination);
  for (size_t i = 0; i < source->height * source->row_bytes; ++i) source->data()[i] = i;
  memset(destination->data(), 0xff, destination->data_size());
  assert(copy_damage(source.get(), destination.get(), {1, 1, 3, 3}) == 16);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 16; ++x) {
      const uint8_t expected = y >= 1 && x >= 4 && x < 12 ? source->data()[y*20+x] : 0xff;
      assert(destination->data()[y*16+x] == expected);
    }
  }
  GRRect accumulated{};
  merge_damage(&accumulated, {2, 2, 3, 3});
  merge_damage(&accumulated, {0, 0, 1, 1});
  assert(accumulated.left == 0 && accumulated.top == 0 &&
         accumulated.right == 3 && accumulated.bottom == 3);
  assert(copy_damage(source.get(), destination.get(), {2, 2, 1, 1}) == 0);
  assert(copy_damage(source.get(), destination.get(), {-10, -10, 10, 10}) == 36);
  auto packed = GRSurface::Create(3, 3, 12, 4);
  auto packed_copy = GRSurface::Create(3, 3, 12, 4);
  memset(packed->data(), 42, packed->data_size());
  assert(copy_damage(packed.get(), packed_copy.get(), {0, 0, 3, 3}) == 36);
  assert(!memcmp(packed->data(), packed_copy->data(), 36));
  auto rgb565 = GRSurface::Create(3, 3, 8, 2);
  assert(copy_damage(packed.get(), rgb565.get(), {0, 0, 3, 3}) == 0);
  puts("PASS: DRM partial/full-row copy, mismatched strides, damage union/clamping, invalid rectangles");
}
