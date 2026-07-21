#include "nk_assets.h"

#include "audio.h"
#include "celutils.h"
#include "debug.h"


bool
nk_asset_bundle_visit(const NkAssetBundle     *bundle_,
                      const NkAssetBundleSpec *spec_,
                      NkAssetBundleVisitor     visitor_,
                      void                    *context_)
{
  CCB *cel;
  u32    index;

  if((bundle_ == NULL) || (bundle_->root == NULL)
     || (spec_ == NULL) || (visitor_ == NULL)
     || (bundle_->count != spec_->count))
    {
      return false;
    }

  cel = bundle_->root;
  for(index = 0U; index < spec_->count; ++index)
    {
      if((cel == NULL) || (!visitor_(context_, index, cel)))
        {
          kprintf("NK2 asset bundle index failed path=%s index=%lu\n",
                  spec_->path,
                  (unsigned long)index);
          return false;
        }

      cel = CEL_NEXTPTR(cel);
    }

  if(cel != NULL)
    {
      kprintf("NK2 asset bundle index failed path=%s extra=1\n",
              spec_->path);
      return false;
    }

  return true;
}


bool
nk_asset_bundle_load(NkAssetBundle           *bundle_,
                     const NkAssetBundleSpec *spec_,
                     uint32                   memory_type_,
                     NkAssetBundleVisitor     visitor_,
                     void                    *context_)
{
  const char *memory_name;
  unsigned long end_time;
  unsigned long start_time;
  CCB *root;

  if((bundle_ == NULL) || (spec_ == NULL) || (visitor_ == NULL))
    {
      return false;
    }

  memory_name = memory_type_ == MEMTYPE_VRAM ? "vram" : "dram";
  if(bundle_->root != NULL)
    {
      if(bundle_->memory_type != memory_type_)
        {
          return false;
        }

      if(!nk_asset_bundle_visit(
           bundle_,
           spec_,
           visitor_,
           context_))
        {
            return false;
          }

      kprintf("NK2 asset bundle cache hit path=%s cels=%lu memory=%s\n",
              spec_->path,
              (unsigned long)bundle_->count,
              memory_name);
      return true;
    }

  start_time = (unsigned long)GetAudioTime();
  root = LoadCel((char *)spec_->path, memory_type_);
  if(root == NULL)
    {
      kprintf("NK2 asset bundle load failed path=%s memory=%s\n",
              spec_->path,
              memory_name);
      return false;
    }

  bundle_->root = root;
  bundle_->count = spec_->count;
  bundle_->memory_type = memory_type_;
  if(!nk_asset_bundle_visit(bundle_, spec_, visitor_, context_))
    {
      UnloadCel(root);
      bundle_->root = NULL;
      bundle_->count = 0U;
      bundle_->memory_type = 0U;
      return false;
    }

  end_time = (unsigned long)GetAudioTime();
  kprintf("NK2 asset bundle loaded path=%s cels=%lu ticks=%lu\n",
          spec_->path,
          (unsigned long)spec_->count,
          end_time - start_time);
  kprintf("NK2 asset bundle memory=%s\n", memory_name);
  return true;
}


void
nk_asset_bundle_unload(NkAssetBundle *bundle_)
{
  if(bundle_ == NULL)
    {
      return;
    }

  if(bundle_->root != NULL)
    {
      UnloadCel(bundle_->root);
      bundle_->root = NULL;
      bundle_->count = 0U;
      bundle_->memory_type = 0U;
    }
}
