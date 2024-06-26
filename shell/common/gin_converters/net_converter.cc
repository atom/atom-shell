// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_converters/net_converter.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "gin/converter.h"
#include "gin/dictionary.h"
#include "gin/object_template_builder.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_version.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/data_element.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "services/network/public/mojom/chunked_data_pipe_getter.mojom.h"
#include "shell/browser/api/electron_api_data_pipe_holder.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/std_converter.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/node_includes.h"

namespace gin {

namespace {

bool CertFromData(const std::string& data,
                  scoped_refptr<net::X509Certificate>* out) {
  auto cert_list = net::X509Certificate::CreateCertificateListFromBytes(
      base::as_bytes(base::make_span(data)),
      net::X509Certificate::FORMAT_SINGLE_CERTIFICATE);
  if (cert_list.empty())
    return false;

  auto leaf_cert = cert_list.front();
  if (!leaf_cert)
    return false;

  *out = leaf_cert;

  return true;
}

}  // namespace

// static
v8::Local<v8::Value> Converter<net::AuthChallengeInfo>::ToV8(
    v8::Isolate* isolate,
    const net::AuthChallengeInfo& val) {
  auto dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("isProxy", val.is_proxy);
  dict.Set("scheme", val.scheme);
  dict.Set("host", val.challenger.host());
  dict.Set("port", static_cast<uint32_t>(val.challenger.port()));
  dict.Set("realm", val.realm);
  return gin::ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<scoped_refptr<net::X509Certificate>>::ToV8(
    v8::Isolate* isolate,
    const scoped_refptr<net::X509Certificate>& val) {
  gin::Dictionary dict(isolate, v8::Object::New(isolate));
  std::string encoded_data;
  net::X509Certificate::GetPEMEncoded(val->cert_buffer(), &encoded_data);

  dict.Set("data", encoded_data);
  dict.Set("issuer", val->issuer());
  dict.Set("issuerName", val->issuer().GetDisplayName());
  dict.Set("subject", val->subject());
  dict.Set("subjectName", val->subject().GetDisplayName());
  dict.Set("serialNumber", base::HexEncode(val->serial_number().data(),
                                           val->serial_number().size()));
  dict.Set("validStart", val->valid_start().InSecondsFSinceUnixEpoch());
  dict.Set("validExpiry", val->valid_expiry().InSecondsFSinceUnixEpoch());
  dict.Set("fingerprint",
           net::HashValue(val->CalculateFingerprint256(val->cert_buffer()))
               .ToString());

  const auto& intermediate_buffers = val->intermediate_buffers();
  if (!intermediate_buffers.empty()) {
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> issuer_intermediates;
    issuer_intermediates.reserve(intermediate_buffers.size() - 1);
    for (size_t i = 1; i < intermediate_buffers.size(); ++i) {
      issuer_intermediates.push_back(
          bssl::UpRef(intermediate_buffers[i].get()));
    }
    const scoped_refptr<net::X509Certificate>& issuer_cert =
        net::X509Certificate::CreateFromBuffer(
            bssl::UpRef(intermediate_buffers[0].get()),
            std::move(issuer_intermediates));
    dict.Set("issuerCert", issuer_cert);
  }

  return ConvertToV8(isolate, dict);
}

bool Converter<scoped_refptr<net::X509Certificate>>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    scoped_refptr<net::X509Certificate>* out) {
  gin::Dictionary dict(nullptr);
  if (!ConvertFromV8(isolate, val, &dict))
    return false;

  std::string data;
  dict.Get("data", &data);
  scoped_refptr<net::X509Certificate> leaf_cert;
  if (!CertFromData(data, &leaf_cert))
    return false;

  scoped_refptr<net::X509Certificate> issuer_cert;
  if (dict.Get("issuerCert", &issuer_cert)) {
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> intermediates;
    intermediates.push_back(bssl::UpRef(issuer_cert->cert_buffer()));
    auto cert = net::X509Certificate::CreateFromBuffer(
        bssl::UpRef(leaf_cert->cert_buffer()), std::move(intermediates));
    if (!cert)
      return false;

    *out = cert;
  } else {
    *out = leaf_cert;
  }

  return true;
}

// static
v8::Local<v8::Value> Converter<net::CertPrincipal>::ToV8(
    v8::Isolate* isolate,
    const net::CertPrincipal& val) {
  gin::Dictionary dict(isolate, v8::Object::New(isolate));

  dict.Set("commonName", val.common_name);
  dict.Set("organizations", val.organization_names);
  dict.Set("organizationUnits", val.organization_unit_names);
  dict.Set("locality", val.locality_name);
  dict.Set("state", val.state_or_province_name);
  dict.Set("country", val.country_name);

  return ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<net::HttpResponseHeaders*>::ToV8(
    v8::Isolate* isolate,
    net::HttpResponseHeaders* headers) {
  base::Value::Dict response_headers;
  if (headers) {
    size_t iter = 0;
    std::string key;
    std::string value;
    while (headers->EnumerateHeaderLines(&iter, &key, &value)) {
      key = base::ToLowerASCII(key);
      base::Value::List* values = response_headers.FindList(key);
      if (!values)
        values = &response_headers.Set(key, base::Value::List())->GetList();
      values->Append(value);
    }
  }
  return ConvertToV8(isolate, response_headers);
}

bool Converter<net::HttpResponseHeaders*>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    net::HttpResponseHeaders* out) {
  if (!val->IsObject()) {
    return false;
  }

  auto addHeaderFromValue = [&isolate, &out](
                                const std::string& key,
                                const v8::Local<v8::Value>& localVal) {
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::String> localStrVal;
    if (!localVal->ToString(context).ToLocal(&localStrVal)) {
      return false;
    }
    std::string value;
    gin::ConvertFromV8(isolate, localStrVal, &value);
    out->AddHeader(key, value);
    return true;
  };

  auto context = isolate->GetCurrentContext();
  auto headers = val.As<v8::Object>();
  auto keys = headers->GetOwnPropertyNames(context).ToLocalChecked();
  for (uint32_t i = 0; i < keys->Length(); i++) {
    v8::Local<v8::Value> keyVal;
    if (!keys->Get(context, i).ToLocal(&keyVal)) {
      return false;
    }
    std::string key;
    gin::ConvertFromV8(isolate, keyVal, &key);

    auto localVal = headers->Get(context, keyVal).ToLocalChecked();
    if (localVal->IsArray()) {
      auto values = localVal.As<v8::Array>();
      for (uint32_t j = 0; j < values->Length(); j++) {
        if (!addHeaderFromValue(key,
                                values->Get(context, j).ToLocalChecked())) {
          return false;
        }
      }
    } else {
      if (!addHeaderFromValue(key, localVal)) {
        return false;
      }
    }
  }
  return true;
}

// static
v8::Local<v8::Value> Converter<net::HttpRequestHeaders>::ToV8(
    v8::Isolate* isolate,
    const net::HttpRequestHeaders& val) {
  gin::Dictionary headers(isolate, v8::Object::New(isolate));
  for (net::HttpRequestHeaders::Iterator it(val); it.GetNext();)
    headers.Set(it.name(), it.value());
  return ConvertToV8(isolate, headers);
}

// static
bool Converter<net::HttpRequestHeaders>::FromV8(v8::Isolate* isolate,
                                                v8::Local<v8::Value> val,
                                                net::HttpRequestHeaders* out) {
  base::Value::Dict dict;
  if (!ConvertFromV8(isolate, val, &dict))
    return false;
  for (const auto it : dict) {
    if (it.second.is_string())
      out->SetHeader(it.first, std::move(it.second).TakeString());
  }
  return true;
}

namespace {

class ChunkedDataPipeReadableStream
    : public gin::Wrappable<ChunkedDataPipeReadableStream> {
 public:
  static gin::Handle<ChunkedDataPipeReadableStream> Create(
      v8::Isolate* isolate,
      network::ResourceRequestBody* request,
      network::DataElementChunkedDataPipe* data_element) {
    return gin::CreateHandle(isolate, new ChunkedDataPipeReadableStream(
                                          isolate, request, data_element));
  }

  // gin::Wrappable
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) override {
    return gin::Wrappable<
               ChunkedDataPipeReadableStream>::GetObjectTemplateBuilder(isolate)
        .SetMethod("read", &ChunkedDataPipeReadableStream::Read);
  }

  const char* GetTypeName() override { return "ChunkedDataPipeReadableStream"; }

  static gin::WrapperInfo kWrapperInfo;

 private:
  ChunkedDataPipeReadableStream(
      v8::Isolate* isolate,
      network::ResourceRequestBody* request,
      network::DataElementChunkedDataPipe* data_element)
      : isolate_(isolate),
        resource_request_body_(request),
        data_element_(data_element),
        handle_watcher_(FROM_HERE,
                        mojo::SimpleWatcher::ArmingPolicy::MANUAL,
                        base::SequencedTaskRunner::GetCurrentDefault()) {}

  ~ChunkedDataPipeReadableStream() override = default;

  int Init() {
    chunked_data_pipe_getter_.Bind(
        data_element_->ReleaseChunkedDataPipeGetter());
    for (auto& element : *resource_request_body_->elements_mutable()) {
      if (element.type() ==
              network::mojom::DataElement::Tag::kChunkedDataPipe &&
          data_element_ == &element.As<network::DataElementChunkedDataPipe>()) {
        element = network::DataElement(
            network::DataElementBytes(std::vector<uint8_t>()));
        break;
      }
    }
    chunked_data_pipe_getter_.set_disconnect_handler(
        base::BindOnce(&ChunkedDataPipeReadableStream::OnDataPipeGetterClosed,
                       base::Unretained(this)));
    chunked_data_pipe_getter_->GetSize(
        base::BindOnce(&ChunkedDataPipeReadableStream::OnSizeReceived,
                       base::Unretained(this)));
    mojo::ScopedDataPipeProducerHandle data_pipe_producer;
    mojo::ScopedDataPipeConsumerHandle data_pipe_consumer;
    MojoResult result =
        mojo::CreateDataPipe(nullptr, data_pipe_producer, data_pipe_consumer);
    if (result != MOJO_RESULT_OK)
      return net::ERR_INSUFFICIENT_RESOURCES;
    chunked_data_pipe_getter_->StartReading(std::move(data_pipe_producer));
    data_pipe_ = std::move(data_pipe_consumer);
    return net::OK;
  }

  v8::Local<v8::Promise> Read(v8::Local<v8::ArrayBufferView> buf) {
    gin_helper::Promise<int> promise(isolate_);
    v8::Local<v8::Promise> handle = promise.GetHandle();

    int status = ReadInternal(buf);

    if (status == net::ERR_IO_PENDING) {
      promise_ = std::move(promise);
    } else {
      if (status < 0)
        std::move(promise).RejectWithErrorMessage(net::ErrorToString(status));
      else
        std::move(promise).Resolve(status);
    }

    return handle;
  }

  int ReadInternal(v8::Local<v8::ArrayBufferView> buf) {
    if (!data_pipe_)
      status_ = Init();
    // If there was an error either passed to the ReadCallback or as a result of
    // closing the DataPipeGetter pipe, fail the read.
    if (status_ != net::OK)
      return status_;

    // Nothing else to do, if the entire body was read.
    if (size_ && bytes_read_ == *size_) {
      // This shouldn't be called if the stream was already completed.
      DCHECK(!is_eof_);

      is_eof_ = true;
      return net::OK;
    }

    if (!handle_watcher_.IsWatching()) {
      handle_watcher_.Watch(
          data_pipe_.get(),
          MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
          base::BindRepeating(&ChunkedDataPipeReadableStream::OnHandleReadable,
                              base::Unretained(this)));
    }

    size_t num_bytes = buf->ByteLength();
    if (size_ && num_bytes > *size_ - bytes_read_)
      num_bytes = *size_ - bytes_read_;
    MojoResult rv = data_pipe_->ReadData(
        static_cast<void*>(static_cast<char*>(buf->Buffer()->Data()) +
                           buf->ByteOffset()),
        &num_bytes, MOJO_READ_DATA_FLAG_NONE);
    if (rv == MOJO_RESULT_OK) {
      bytes_read_ += num_bytes;
      // Not needed for correctness, but this allows the consumer to send the
      // final chunk and the end of stream message together, for protocols that
      // allow it.
      if (size_ && *size_ == bytes_read_)
        is_eof_ = true;
      return num_bytes;
    }

    if (rv == MOJO_RESULT_SHOULD_WAIT) {
      handle_watcher_.ArmOrNotify();
      buf_.Reset(isolate_, buf);
      return net::ERR_IO_PENDING;
    }

    // The pipe was closed. If the size isn't known yet, could be a success or a
    // failure.
    if (!size_) {
      // Need to keep the buffer around because its presence is used to indicate
      // that there's a pending UploadDataStream read.
      buf_.Reset(isolate_, buf);

      handle_watcher_.Cancel();
      data_pipe_.reset();
      return net::ERR_IO_PENDING;
    }

    // |size_| was checked earlier, so if this point is reached, the pipe was
    // closed before receiving all bytes.
    DCHECK_LT(bytes_read_, *size_);

    return net::ERR_FAILED;
  }

  void OnSizeReceived(int32_t status, uint64_t size) {
    DCHECK(!size_);
    DCHECK_EQ(net::OK, status_);

    status_ = status;
    if (status == net::OK) {
      size_ = size;
      if (size == bytes_read_) {
        // Only set this as a final chunk if there's a read in progress. Setting
        // it asynchronously could result in confusing consumers.
        if (!buf_.IsEmpty())
          is_eof_ = true;
      } else if (size < bytes_read_ ||
                 (!buf_.IsEmpty() && !data_pipe_.is_valid())) {
        // If more data was received than was expected, or there's a pending
        // read and data pipe was closed without passing in as many bytes as
        // expected, the upload can't continue.  If there's no pending read but
        // the pipe was closed, the closure and size difference will be noticed
        // on the next read attempt.
        status_ = net::ERR_FAILED;
      }
    }

    // If this is done, and there's a pending read, complete the pending read.
    // If there's not a pending read, either |status_| will be reported on the
    // next read, the file will be marked as done, so ReadInternal() won't be
    // called again.
    if (!buf_.IsEmpty() && (is_eof_ || status_ != net::OK)) {
      // |data_pipe_| isn't needed any more, and if it's still open, a close
      // pipe message would cause issues, since this class normally only watches
      // the pipe when there's a pending read.
      handle_watcher_.Cancel();
      data_pipe_.reset();
      // Clear |buf_| as well, so it's only non-null while there's a pending
      // read.
      buf_.Reset();
      chunked_data_pipe_getter_.reset();

      OnReadCompleted(status_);

      // |this| may have been deleted at this point.
    }
  }

  void OnHandleReadable(MojoResult result) {
    DCHECK(!buf_.IsEmpty());

    v8::HandleScope handle_scope(isolate_);

    v8::Local<v8::ArrayBufferView> buf = buf_.Get(isolate_);
    buf_.Reset();

    int rv = ReadInternal(buf);

    if (rv != net::ERR_IO_PENDING)
      OnReadCompleted(rv);

    // |this| may have been deleted at this point.
  }

  void OnReadCompleted(int result) {
    if (result < 0)
      std::move(promise_).RejectWithErrorMessage(net::ErrorToString(result));
    else
      std::move(promise_).Resolve(result);
  }

  void OnDataPipeGetterClosed() {
    // If the size hasn't been received yet, treat this as receiving an error.
    // Otherwise, this will only be a problem if/when InitInternal() tries to
    // start reading again, so do nothing.
    if (status_ == net::OK && !size_)
      OnSizeReceived(net::ERR_FAILED, 0);
  }

  raw_ptr<v8::Isolate> isolate_;
  int status_ = net::OK;
  scoped_refptr<network::ResourceRequestBody> resource_request_body_;
  raw_ptr<network::DataElementChunkedDataPipe> data_element_;
  mojo::Remote<network::mojom::ChunkedDataPipeGetter> chunked_data_pipe_getter_;
  mojo::ScopedDataPipeConsumerHandle data_pipe_;
  mojo::SimpleWatcher handle_watcher_;
  std::optional<uint64_t> size_;
  uint64_t bytes_read_ = 0;
  bool is_eof_ = false;
  v8::Global<v8::ArrayBufferView> buf_;
  gin_helper::Promise<int> promise_;
};

gin::WrapperInfo ChunkedDataPipeReadableStream::kWrapperInfo = {
    gin::kEmbedderNativeGin};

}  // namespace

// static
v8::Local<v8::Value> Converter<network::ResourceRequestBody>::ToV8(
    v8::Isolate* isolate,
    const network::ResourceRequestBody& val) {
  const auto& elements = *val.elements();
  v8::Local<v8::Array> arr = v8::Array::New(isolate, elements.size());
  for (size_t i = 0; i < elements.size(); ++i) {
    const auto& element = elements[i];
    gin::Dictionary upload_data(isolate, v8::Object::New(isolate));
    switch (element.type()) {
      case network::mojom::DataElement::Tag::kFile: {
        const auto& element_file = element.As<network::DataElementFile>();
        upload_data.Set("type", "file");
        upload_data.Set("file", element_file.path().value());
        upload_data.Set("filePath",
                        base::Value(element_file.path().AsUTF8Unsafe()));
        upload_data.Set("offset", static_cast<int>(element_file.offset()));
        upload_data.Set("length", static_cast<int>(element_file.length()));
        upload_data.Set("modificationTime",
                        element_file.expected_modification_time()
                            .InSecondsFSinceUnixEpoch());
        break;
      }
      case network::mojom::DataElement::Tag::kBytes: {
        upload_data.Set("type", "rawData");
        const auto& bytes = element.As<network::DataElementBytes>().bytes();
        const char* data = reinterpret_cast<const char*>(bytes.data());
        upload_data.Set(
            "bytes",
            node::Buffer::Copy(isolate, data, bytes.size()).ToLocalChecked());
        break;
      }
      case network::mojom::DataElement::Tag::kDataPipe: {
        upload_data.Set("type", "blob");
        // TODO(zcbenz): After the NetworkService refactor, the old blobUUID API
        // becomes unnecessarily complex, we should deprecate the getBlobData
        // API and return the DataPipeHolder wrapper directly.
        auto holder = electron::api::DataPipeHolder::Create(isolate, element);
        upload_data.Set("blobUUID", holder->id());
        // The lifetime of data pipe is bound to the uploadData object.
        upload_data.Set("dataPipe", holder);
        break;
      }
      case network::mojom::DataElement::Tag::kChunkedDataPipe: {
        upload_data.Set("type", "stream");
        // ReleaseChunkedDataPipeGetter mutates the element, but unfortunately
        // gin converters are only allowed const references, so we need to cast
        // off the const here.
        auto& mutable_element =
            const_cast<network::DataElementChunkedDataPipe&>(
                element.As<network::DataElementChunkedDataPipe>());
        upload_data.Set(
            "body",
            ChunkedDataPipeReadableStream::Create(
                isolate, const_cast<network::ResourceRequestBody*>(&val),
                &mutable_element));
        break;
      }
      default:
        NOTREACHED() << "Found unsupported data element";
    }
    arr->Set(isolate->GetCurrentContext(), static_cast<uint32_t>(i),
             ConvertToV8(isolate, upload_data))
        .Check();
  }
  return arr;
}

// static
v8::Local<v8::Value>
Converter<scoped_refptr<network::ResourceRequestBody>>::ToV8(
    v8::Isolate* isolate,
    const scoped_refptr<network::ResourceRequestBody>& val) {
  if (!val)
    return v8::Null(isolate);
  return ConvertToV8(isolate, *val);
}

// static
bool Converter<scoped_refptr<network::ResourceRequestBody>>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    scoped_refptr<network::ResourceRequestBody>* out) {
  base::Value list_value;
  if (!ConvertFromV8(isolate, val, &list_value) || !list_value.is_list())
    return false;
  base::Value::List& list = list_value.GetList();
  *out = base::MakeRefCounted<network::ResourceRequestBody>();
  for (base::Value& dict_value : list) {
    if (!dict_value.is_dict())
      return false;
    base::Value::Dict& dict = dict_value.GetDict();
    std::string* type = dict.FindString("type");
    if (!type)
      return false;
    if (*type == "rawData") {
      const base::Value::BlobStorage* bytes = dict.FindBlob("bytes");
      (*out)->AppendBytes(reinterpret_cast<const char*>(bytes->data()),
                          base::checked_cast<int>(bytes->size()));
    } else if (*type == "file") {
      const std::string* file = dict.FindString("filePath");
      if (!file)
        return false;
      double modification_time =
          dict.FindDouble("modificationTime").value_or(0.0);
      int offset = dict.FindInt("offset").value_or(0);
      int length = dict.FindInt("length").value_or(-1);
      (*out)->AppendFileRange(
          base::FilePath::FromUTF8Unsafe(*file), static_cast<uint64_t>(offset),
          static_cast<uint64_t>(length),
          base::Time::FromSecondsSinceUnixEpoch(modification_time));
    }
  }
  return true;
}

// static
v8::Local<v8::Value> Converter<network::ResourceRequest>::ToV8(
    v8::Isolate* isolate,
    const network::ResourceRequest& val) {
  auto dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("method", val.method);
  dict.Set("url", val.url.spec());
  dict.Set("referrer", val.referrer.spec());
  dict.Set("headers", val.headers);
  if (val.request_body)
    dict.Set("uploadData", ConvertToV8(isolate, *val.request_body));
  return ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<electron::VerifyRequestParams>::ToV8(
    v8::Isolate* isolate,
    electron::VerifyRequestParams val) {
  auto dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("hostname", val.hostname);
  dict.Set("certificate", val.certificate);
  dict.Set("validatedCertificate", val.validated_certificate);
  dict.Set("isIssuedByKnownRoot", val.is_issued_by_known_root);
  dict.Set("verificationResult", val.default_result);
  dict.Set("errorCode", val.error_code);
  return ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<net::HttpVersion>::ToV8(
    v8::Isolate* isolate,
    const net::HttpVersion& val) {
  auto dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("major", static_cast<uint32_t>(val.major_value()));
  dict.Set("minor", static_cast<uint32_t>(val.minor_value()));
  return ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<net::RedirectInfo>::ToV8(
    v8::Isolate* isolate,
    const net::RedirectInfo& val) {
  auto dict = gin::Dictionary::CreateEmpty(isolate);

  dict.Set("statusCode", val.status_code);
  dict.Set("newMethod", val.new_method);
  dict.Set("newUrl", val.new_url);
  dict.Set("newSiteForCookies", val.new_site_for_cookies.RepresentativeUrl());
  dict.Set("newReferrer", val.new_referrer);
  dict.Set("insecureSchemeWasUpgraded", val.insecure_scheme_was_upgraded);
  dict.Set("isSignedExchangeFallbackRedirect",
           val.is_signed_exchange_fallback_redirect);

  return ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<net::IPEndPoint>::ToV8(
    v8::Isolate* isolate,
    const net::IPEndPoint& val) {
  gin::Dictionary dict(isolate, v8::Object::New(isolate));
  dict.Set("address", val.ToStringWithoutPort());
  switch (val.GetFamily()) {
    case net::ADDRESS_FAMILY_IPV4: {
      dict.Set("family", "ipv4");
      break;
    }
    case net::ADDRESS_FAMILY_IPV6: {
      dict.Set("family", "ipv6");
      break;
    }
    case net::ADDRESS_FAMILY_UNSPECIFIED: {
      dict.Set("family", "unspec");
      break;
    }
  }
  return ConvertToV8(isolate, dict);
}

// static
bool Converter<net::DnsQueryType>::FromV8(v8::Isolate* isolate,
                                          v8::Local<v8::Value> val,
                                          net::DnsQueryType* out) {
  static constexpr auto Lookup =
      base::MakeFixedFlatMap<std::string_view, net::DnsQueryType>({
          {"A", net::DnsQueryType::A},
          {"AAAA", net::DnsQueryType::AAAA},
      });
  return FromV8WithLookup(isolate, val, Lookup, out);
}

// static
bool Converter<net::HostResolverSource>::FromV8(v8::Isolate* isolate,
                                                v8::Local<v8::Value> val,
                                                net::HostResolverSource* out) {
  using Val = net::HostResolverSource;
  static constexpr auto Lookup = base::MakeFixedFlatMap<std::string_view, Val>({
      {"any", Val::ANY},
      {"dns", Val::DNS},
      {"localOnly", Val::LOCAL_ONLY},
      {"mdns", Val::MULTICAST_DNS},
      {"system", Val::SYSTEM},
  });
  return FromV8WithLookup(isolate, val, Lookup, out);
}

// static
bool Converter<network::mojom::ResolveHostParameters::CacheUsage>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    network::mojom::ResolveHostParameters::CacheUsage* out) {
  using Val = network::mojom::ResolveHostParameters::CacheUsage;
  static constexpr auto Lookup = base::MakeFixedFlatMap<std::string_view, Val>({
      {"allowed", Val::ALLOWED},
      {"disallowed", Val::DISALLOWED},
      {"staleAllowed", Val::STALE_ALLOWED},
  });
  return FromV8WithLookup(isolate, val, Lookup, out);
}

// static
bool Converter<network::mojom::SecureDnsPolicy>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    network::mojom::SecureDnsPolicy* out) {
  using Val = network::mojom::SecureDnsPolicy;
  static constexpr auto Lookup = base::MakeFixedFlatMap<std::string_view, Val>({
      {"allow", Val::ALLOW},
      {"disable", Val::DISABLE},
  });
  return FromV8WithLookup(isolate, val, Lookup, out);
}

// static
bool Converter<network::mojom::ResolveHostParametersPtr>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    network::mojom::ResolveHostParametersPtr* out) {
  gin::Dictionary dict(nullptr);
  if (!ConvertFromV8(isolate, val, &dict))
    return false;

  network::mojom::ResolveHostParametersPtr params =
      network::mojom::ResolveHostParameters::New();

  dict.Get("queryType", &(params->dns_query_type));
  dict.Get("source", &(params->source));
  dict.Get("cacheUsage", &(params->cache_usage));
  dict.Get("secureDnsPolicy", &(params->secure_dns_policy));

  *out = std::move(params);
  return true;
}

}  // namespace gin
