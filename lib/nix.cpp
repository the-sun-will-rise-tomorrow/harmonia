#include <nix/config.h>

#include <nix/derivations.hh>
#include <nix/globals.hh>
#include <nix/store-api.hh>
#include <nix/util.hh>
#include <nix/crypto.hh>

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// This is illegal but i dont care
#define DEFAULT_TRY_CATCH(block)                                               \
  try {                                                                        \
    block                                                                      \
  } catch (const nix::Error &e) {                                              \
    fprintf(stderr, "%s\n", e.what());                                         \
  }

static nix::ref<nix::Store> store() {
  static std::shared_ptr<nix::Store> _store;
  if (!_store) {
    DEFAULT_TRY_CATCH({
      nix::loadConfFile();
      nix::settings.lockCPU = false;
      _store = nix::openStore();
    })
  }
  return nix::ref<nix::Store>(_store);
}

static const char *str_dup(std::string_view str) {
  char *ret = (char *)malloc(str.size() + 1);
  strncpy(ret, str.data(), str.size());
  ret[str.size()] = '\0';
  return ret;
}

extern "C" {
#include "nix.h"

static nix_str_array_t init_arr(size_t size) {
  return (nix_str_array_t){(const char **)malloc(sizeof(char *) * size), size};
}

static nix_str_hash_t init_hash(size_t size) {
  return (nix_str_hash_t){
      (nix_str_tuple_t **)malloc(sizeof(nix_str_tuple_t *) * size), size};
}

void nix_init() {
  store();
}

void nix_set_verbosity(int32_t level) {
  nix::verbosity = (nix::Verbosity)level;
}

bool nix_is_valid_path(const char *path) {
  DEFAULT_TRY_CATCH(return store()->isValidPath(store()->parseStorePath(path));)
  return false;
}

SV *nix_query_references(const char *path) {
  DEFAULT_TRY_CATCH({
    std::vector<std::string> ret;
    // TODO(conni2461): RETURN this array ^
    for (const nix::StorePath &store_path :
         store()->queryPathInfo(store()->parseStorePath(path))->references) {
      ret.push_back(store()->printStorePath(store_path).c_str());
    }
  })
  return NULL;
}

const char *nix_query_path_hash(const char *path) {
  DEFAULT_TRY_CATCH({
    std::string s = store()
                        ->queryPathInfo(store()->parseStorePath(path))
                        ->narHash.to_string(nix::Base32, true);
    return str_dup(s);
  })
  return NULL;
}

typedef void SV;

const char *nix_query_deriver(const char *path) {
  DEFAULT_TRY_CATCH({
    nix::ref<const nix::ValidPathInfo> info =
        store()->queryPathInfo(store()->parseStorePath(path));
    if (!info->deriver) {
      return NULL;
    }
    return str_dup(store()->printStorePath(*info->deriver));
  })
  return NULL;
}

const nix_path_info_t *nix_query_path_info(const char *path, bool base32) {
  nix_path_info_t *res = (nix_path_info_t *)malloc(sizeof(nix_path_info_t));
  DEFAULT_TRY_CATCH({
    nix::ref<const nix::ValidPathInfo> info =
        store()->queryPathInfo(store()->parseStorePath(path));
    if (!info->deriver) {
      res->drv = NULL;
    } else {
      res->drv = str_dup(store()->printStorePath(*info->deriver));
    }
    std::string s =
        info->narHash.to_string(base32 ? nix::Base32 : nix::Base16, true);
    res->narhash = str_dup(s);
    res->time = info->registrationTime;
    res->size = info->narSize;

    res->refs = init_arr(info->references.size());
    size_t idx = 0;
    for (const nix::StorePath &i : info->references) {
      res->refs.arr[idx] = str_dup(store()->printStorePath(i));
      ++idx;
    }

    res->sigs = init_arr(info->sigs.size());
    idx = 0;
    for (const std::string &i : info->sigs) {
      res->sigs.arr[idx] = str_dup(i);
      ++idx;
    }
    return res;
  })
  free(res);
  return NULL;
}

const char *nix_query_raw_realisation(const char *output_id) {
  DEFAULT_TRY_CATCH({
    std::shared_ptr<const nix::Realisation> realisation =
        store()->queryRealisation(nix::DrvOutput::parse(output_id));
    if (!realisation) {
      return str_dup("");
    }
    return str_dup(realisation->toJSON().dump());
  })
  return NULL;
}

const char *nix_query_path_from_hash_part(const char *hash_part) {
  DEFAULT_TRY_CATCH({
    std::optional<nix::StorePath> path =
        store()->queryPathFromHashPart(hash_part);
    // TODO(conni2461): Rust code would be easier if this is a nullptr if not
    // found
    return str_dup(path ? store()->printStorePath(*path) : "");
  })
  return NULL;
}

SV *nix_compute_fs_closure(bool flip_direction, bool include_outputs,
                           const char **paths) {
  DEFAULT_TRY_CATCH({
    nix::StorePathSet path_set;
    for (size_t i = 0; paths[i] != NULL; ++i) {
      store()->computeFSClosure(store()->parseStorePath(paths[i]), path_set,
                                flip_direction, include_outputs);
    }
    // TODO(conni2461): RETURN this array ^
    // for (auto & i : paths)
    //     XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(i).c_str(), 0)));
  })
  return NULL;
}

SV *nix_topo_sort_paths(const char **paths) {
  DEFAULT_TRY_CATCH({
    nix::StorePathSet path_set;
    for (size_t i = 0; paths[i] != NULL; ++i) {
      path_set.insert(store()->parseStorePath(paths[i]));
    }
    nix::StorePaths sorted = store()->topoSortPaths(path_set);
    // TODO(conni2461): RETURN this array ^
    // for (auto & i : sorted)
    //     XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(i).c_str(), 0)));
  })
  return NULL;
}

const char *nix_follow_links_to_store_path(const char *path) {
  DEFAULT_TRY_CATCH({
    return str_dup(
        store()->printStorePath(store()->followLinksToStorePath(path)));
  })
  return NULL;
}

const char *nix_export_path(const char *path, size_t size) {
  nix::StringSink sink(size);
  store()->exportPath(store()->parseStorePath(path), sink);
  char *ret = (char *)malloc(size + 1);
  memcpy(ret, sink.s.c_str(), size);
  ret[size] = '\0';
  return ret;
}

void nix_export_paths(int32_t fd, const char **paths) {
  DEFAULT_TRY_CATCH({
    nix::StorePathSet path_set;
    for (size_t i = 0; paths[i] != NULL; ++i) {
      path_set.insert(store()->parseStorePath(paths[i]));
    }
    nix::FdSink sink(fd);
    store()->exportPaths(path_set, sink);
  })
}

void nix_import_paths(int32_t fd, bool dont_check_signs) {
  DEFAULT_TRY_CATCH({
    nix::FdSource source(fd);
    store()->importPaths(source,
                         dont_check_signs ? nix::NoCheckSigs : nix::CheckSigs);
  })
}

const char *nix_hash_path(const char *algo, bool base32, const char *path) {
  DEFAULT_TRY_CATCH({
    nix::Hash h = nix::hashPath(nix::parseHashType(algo), path).first;
    std::string s = h.to_string(base32 ? nix::Base32 : nix::Base16, false);
    return str_dup(s);
  })
  return NULL;
}

const char *nix_hash_file(const char *algo, bool base32, const char *path) {
  DEFAULT_TRY_CATCH({
    nix::Hash h = nix::hashFile(nix::parseHashType(algo), path);
    std::string s = h.to_string(base32 ? nix::Base32 : nix::Base16, false);
    return str_dup(s);
  })
  return NULL;
}

const char *nix_hash_string(const char *algo, bool base32, const char *s) {
  DEFAULT_TRY_CATCH({
    nix::Hash h = nix::hashString(nix::parseHashType(algo), s);
    std::string s = h.to_string(base32 ? nix::Base32 : nix::Base16, false);
    return str_dup(s);
  })
  return NULL;
}

const char *nix_convert_hash(const char *algo, const char *s, bool to_base_32) {
  DEFAULT_TRY_CATCH({
    nix::Hash h = nix::Hash::parseAny(s, nix::parseHashType(algo));
    std::string s = h.to_string(to_base_32 ? nix::Base32 : nix::Base16, false);
    return str_dup(s);
  })
  return NULL;
}

const char *nix_sign_string(const char *secret_key, const char *msg) {
  DEFAULT_TRY_CATCH({
    std::string sig = nix::SecretKey(secret_key).signDetached(msg);
    return str_dup(sig);
  })
  return NULL;
}

bool nix_check_signature(const char *public_key, const char *sig,
                         const char *msg) {
  DEFAULT_TRY_CATCH({
    size_t public_key_len = strlen(public_key);
    if (public_key_len != crypto_sign_PUBLICKEYBYTES) {
      throw nix::Error("public key is not valid");
    }

    size_t sig_len = strlen(sig);
    if (sig_len != crypto_sign_BYTES) {
      throw nix::Error("signature is not valid");
    }
    return crypto_sign_verify_detached((unsigned char *)sig,
                                       (unsigned char *)msg, strlen(msg),
                                       (unsigned char *)public_key) == 0;
  })
  return false;
}

const char *nix_add_to_store(const char *src_path, int32_t recursive,
                             const char *algo) {
  DEFAULT_TRY_CATCH({
    nix::FileIngestionMethod method = recursive
                                          ? nix::FileIngestionMethod::Recursive
                                          : nix::FileIngestionMethod::Flat;
    nix::StorePath path =
        store()->addToStore(std::string(nix::baseNameOf(src_path)), src_path,
                            method, nix::parseHashType(algo));
    return str_dup(store()->printStorePath(path));
  })
  return NULL;
}

const char *nix_make_fixed_output_path(bool recursive, const char *algo,
                                       const char *hash, const char *name) {
  DEFAULT_TRY_CATCH({
    nix::Hash h = nix::Hash::parseAny(hash, nix::parseHashType(algo));
    nix::FileIngestionMethod method = recursive
                                          ? nix::FileIngestionMethod::Recursive
                                          : nix::FileIngestionMethod::Flat;
    nix::StorePath path = store()->makeFixedOutputPath(method, h, name);
    return str_dup(store()->printStorePath(path));
  })
  return NULL;
}

const nix_drv_t *nix_derivation_from_path(const char *drv_path) {
  nix_drv_t *res = (nix_drv_t *)malloc(sizeof(nix_drv_t));
  size_t idx = 0;
  try {
    nix::Derivation drv =
        store()->derivationFromPath(store()->parseStorePath(drv_path));

    res->outputs = init_hash(drv.outputsAndOptPaths(*store()).size());
    for (auto &i : drv.outputsAndOptPaths(*store())) {
      res->outputs.arr[idx] =
          (nix_str_tuple_t *)malloc(sizeof(nix_str_tuple_t));
      res->outputs.arr[idx]->lhs = str_dup(i.first);
      res->outputs.arr[idx]->rhs =
          !i.second.second ? NULL
                           : str_dup(store()->printStorePath(*i.second.second));
      ++idx;
    }

    res->input_drvs = init_arr(drv.inputDrvs.size());
    idx = 0;
    for (auto &i : drv.inputDrvs) {
      res->input_drvs.arr[idx] = str_dup(store()->printStorePath(i.first));
      ++idx;
    }

    res->input_srcs = init_arr(drv.inputSrcs.size());
    idx = 0;
    for (auto &i : drv.inputSrcs) {
      res->input_srcs.arr[idx] = str_dup(store()->printStorePath(i));
      ++idx;
    }

    res->platform = str_dup(drv.platform);
    res->builder = str_dup(drv.builder);

    res->args = init_arr(drv.args.size());
    idx = 0;
    for (const std::string &i : drv.args) {
      res->args.arr[idx] = str_dup(i);
      ++idx;
    }

    res->env = init_hash(drv.env.size());
    idx = 0;
    for (auto &i : drv.env) {
      res->env.arr[idx] = (nix_str_tuple_t *)malloc(sizeof(nix_str_tuple_t));
      res->env.arr[idx]->lhs = str_dup(i.first);
      res->env.arr[idx]->rhs = str_dup(i.second);
      ++idx;
    }
    return res;
  } catch (const nix::Error &e) {
    fprintf(stderr, "%s\n", e.what());
  }
  free(res);
  return NULL;
}

void nix_add_temp_root(const char *store_path) {
  DEFAULT_TRY_CATCH(store()->addTempRoot(store()->parseStorePath(store_path));)
}

const char *nix_get_bin_dir() {
  return str_dup(nix::settings.nixBinDir);
}

const char *nix_get_store_dir() {
  return str_dup(nix::settings.nixStore);
}
}