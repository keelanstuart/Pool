# Pool
Pool, a thread-pooled asynchronous job library with an easy-to-use API


# Pseudocode examples

*************************************************

// A simple, but realistic sample: pseudocode that does something to a fictional image, in chunks.
// Verbosity for explanation's sake.
// Image is a hypothetical class that wraps RGB image data. Use your imagination.

#include <pool.h>

#define HORZ_CHUNKS   8
#define VERT_CHUNKS   8

void __cdecl ProcessChunk(Image *input_image, LPVOID dummy, size_t chunk_number)
{
  // compute which chunk we're looking at in the 8x8 grid
  int vertical_chunk = chunk_number / VERT_CHUNKS;
  int horizontal_chunk = chunk_number % HORZ_CHUNKS;

  // how big are the chunks?
  int chunk_width = input_image->Width() / HORZ_CHUNKS;
  int chunk_height = input_image->Height() / VERT_CHUNKS;

  // get the bounds for x
  int start_x = chunk_width * horizontal_chunk;
  int end_x = (chunk_width + 1) * horizontal_chunk;

  // get the bounds for y
  int start_y = chunk_height * vertical_chunk;
  int end_y = (chunk_height + 1) * vertical_chunk;

  struct Pixel
  {
    BYTE r, g, b;
  } *pix;

  // Offset from the image data to the corner of our chunk
  pix = (Pixel *)input_image->Data() + (start_y * input_image->Width());

  for (int y = start_y; y < end_y; y++)
  {
    for (int x = start_x; x < end_x; x++)
    {
      // invert color components
      pix[x].r = 255 - pix[x].r;
      pix[x].g = 255 - pix[x].g;
      pix[x].b = 255 - pix[x].b;
    }

    // add the stride to get the next line
    pix += input_image->Width();
  }
}

int main()
{
  // Creates a thread pool that has 2 threads per CPU core
  // If you were using a machine that had no hyperthreading but multiple cores, you might use only 1 thread/core
  IThreadPool *ppool = pool::IThreadPool::Create(2, 0);
	
  if (!ppool)
    return -1;

  // Load an image here
  Image *input_image = Image::Load("image1.png");

  // Run ProcessChunk for each image chunk and block here until all tasks are complete
  // ProcessChunk assumes an RGB image that is evenly divisible by 8 in both dimensions
  ppool->RunTask(ProcessChunk, (LPVOID)input_image, nullptr, VERT_CHUNKS * HORZ_CHUNKS, true);

  // Save the image back to disk
  input_image->Save("image2.png");

  // Release our thread pool
  ppool->Release();
}

*************************************************

You can also create a "pool" with 0 threads, add tasks from multiple threads, then execute them all on a single thread later
by calling Flush. This is useful for graphics tasks, for example, where you may want to load texture or geometry data
asynchronously but then upload to GPU memory in the main render thread.
