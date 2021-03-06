/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/db/query/getmore_request.h"

#include <boost/optional.hpp>

#include "mongo/db/api_parameters_gen.h"
#include "mongo/db/commands.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/query/getmore_command_gen.h"
#include "mongo/db/repl/bson_extract_optime.h"
#include "mongo/idl/command_generic_argument.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/str.h"

namespace mongo {

GetMoreRequest::GetMoreRequest() : cursorid(0), batchSize(0) {}

GetMoreRequest::GetMoreRequest(NamespaceString namespaceString,
                               CursorId id,
                               boost::optional<std::int64_t> sizeOfBatch,
                               boost::optional<Milliseconds> awaitDataTimeout,
                               boost::optional<long long> term,
                               boost::optional<repl::OpTime> lastKnownCommittedOpTime)
    : nss(std::move(namespaceString)),
      cursorid(id),
      batchSize(sizeOfBatch),
      awaitDataTimeout(awaitDataTimeout),
      term(term),
      lastKnownCommittedOpTime(lastKnownCommittedOpTime) {}

Status GetMoreRequest::isValid() const {
    if (!nss.isValid()) {
        return Status(ErrorCodes::InvalidNamespace,
                      str::stream() << "Invalid namespace for getMore: " << nss.ns());
    }

    if (cursorid == 0) {
        return Status(ErrorCodes::BadValue, "Cursor id for getMore must be non-zero");
    }

    if (batchSize && *batchSize <= 0) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Batch size for getMore must be positive, "
                                    << "but received: " << *batchSize);
    }

    return Status::OK();
}

// static
StatusWith<GetMoreRequest> GetMoreRequest::parseFromBSON(const std::string& dbname,
                                                         const BSONObj& cmdObj) try {
    for (const auto& fieldName :
         std::vector<StringData>{APIParametersFromClient::kApiVersionFieldName,
                                 APIParametersFromClient::kApiStrictFieldName,
                                 APIParametersFromClient::kApiDeprecationErrorsFieldName}) {
        uassert(4937600,
                str::stream() << "Cannot pass in API parameter field " << fieldName,
                !cmdObj.hasField(fieldName));
    }

    auto parsed = GetMoreCommand::parse({"getMore"}, cmdObj);
    auto maxTimeMS = parsed.getMaxTimeMS();

    GetMoreRequest request(
        NamespaceString(dbname, parsed.getCollection()),
        parsed.getCommandParameter(),
        parsed.getBatchSize(),
        // Treat maxTimeMS=0 the same as none.
        (maxTimeMS && *maxTimeMS) ? boost::optional<Milliseconds>(*maxTimeMS) : boost::none,
        parsed.getTerm() ? boost::optional<long long>(*parsed.getTerm()) : boost::none,
        parsed.getLastKnownCommittedOpTime());

    Status validStatus = request.isValid();
    if (!validStatus.isOK()) {
        return validStatus;
    }

    return request;
} catch (const DBException& exc) {
    return exc.toStatus();
}

BSONObj GetMoreRequest::toBSON() const {
    auto cmd = GetMoreCommand(cursorid);
    cmd.setDbName(nss.db());
    cmd.setCollection(nss.coll());
    cmd.setBatchSize(batchSize);
    cmd.setLastKnownCommittedOpTime(lastKnownCommittedOpTime);
    if (term) {
        cmd.setTerm(static_cast<int64_t>(*term));
    }
    if (awaitDataTimeout) {
        cmd.setMaxTimeMS(durationCount<Milliseconds>(*awaitDataTimeout));
    }

    return cmd.toBSON({});
}

}  // namespace mongo
