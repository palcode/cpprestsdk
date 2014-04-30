/***
* ==++==
*
* Copyright (c) Microsoft Corporation. All rights reserved. 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* ==--==
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* oauth2_tests.cpp
*
* Test cases for oauth2.
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/

#include "stdafx.h"

using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::details;
using namespace utility;
using namespace concurrency;

using namespace tests::functional::http::utilities;

namespace tests { namespace functional { namespace http { namespace client {

SUITE(oauth2_tests)
{

class oauth2_test_uri
{
public:
    oauth2_test_uri() : m_uri(U("http://localhost:16743/")) {}
    web::http::uri m_uri;
};

TEST(oauth2_build_authorization_uri)
{
    oauth2_config c(U(""), U(""), U(""), U(""), U(""));

    // Empty authorization URI.
    {
        VERIFY_ARE_EQUAL(U("/?response_type=code&client_id=&redirect_uri=&state="), c.build_authorization_uri(U("")));
    }

    // Authorization URI with scope parameter.
    {
        c.set_scope(U("testing_123"));
        VERIFY_ARE_EQUAL(U("/?response_type=code&client_id=&redirect_uri=&state=&scope=testing_123"), c.build_authorization_uri(U("")));
    }

    // Setters/getters, full authorization URI with both state and scope.
    {
        c.set_client_key(U("4567abcd"));
        c.set_auth_endpoint(U("https://foo"));
        c.set_redirect_uri(U("http://localhost:8080"));
        VERIFY_ARE_EQUAL(U("https://foo/?response_type=code&client_id=4567abcd&redirect_uri=http://localhost:8080&state=xuzzy&scope=testing_123"),
                c.build_authorization_uri(U("xuzzy")));
    }
}

TEST_FIXTURE(oauth2_test_uri, oauth2_fetch_token)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(U("123ABC"), U("456DEF"), U("https://foo"), m_uri.to_string(), U("https://bar"));

    VERIFY_ARE_EQUAL(false, c.is_enabled());

    // Fetch using HTTP Basic authentication.
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            utility::string_t content, charset;
            parse_content_type_and_charset(request->m_headers[header_names::content_type], content, charset);
            VERIFY_ARE_EQUAL(mime_types::application_x_www_form_urlencoded, content);

            VERIFY_ARE_EQUAL(U("Basic MTIzQUJDOjQ1NkRFRg=="), request->m_headers[header_names::authorization]);

            VERIFY_ARE_EQUAL(conversions::to_body_data(
                    U("grant_type=authorization_code&code=789GHI&redirect_uri=https%3A%2F%2Fbar")),
                    request->m_body);

            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            // NOTE: Reply body data must not be wide chars.
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"xuzzy123\"}");
        });

        c.fetch_token(U("789GHI")).wait();
        VERIFY_ARE_EQUAL(U("xuzzy123"), c.token());
        VERIFY_ARE_EQUAL(true, c.is_enabled());
    }

    // Fetch using client key & secret in request body (x-www-form-urlencoded).
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            utility::string_t content;
            utility::string_t charset;
            parse_content_type_and_charset(request->m_headers[header_names::content_type], content, charset);
            VERIFY_ARE_EQUAL(mime_types::application_x_www_form_urlencoded, content);

            VERIFY_ARE_EQUAL(U(""), request->m_headers[header_names::authorization]);

            VERIFY_ARE_EQUAL(conversions::to_body_data(
                    U("grant_type=authorization_code&code=789GHI&redirect_uri=https%3A%2F%2Fbar&client_id=123ABC&client_secret=456DEF")),
                    request->m_body);

            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            // NOTE: Reply body data must not be wide chars.
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"xuzzy123\"}");
        });

        c.set_token(U(""));
        VERIFY_ARE_EQUAL(false, c.is_enabled());
        c.fetch_token(U("789GHI"), false).wait();
        VERIFY_ARE_EQUAL(U("xuzzy123"), c.token());
        VERIFY_ARE_EQUAL(true, c.is_enabled());
    }
}

TEST_FIXTURE(oauth2_test_uri, oauth2_bearer_token)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(U("12345678"));
    http_client_config config;

    // Default, bearer token in "Authorization" header (bearer_auth() == true)
    {
        config.set_oauth2(c);

        http_client client(m_uri, config);
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(U("Bearer 12345678"), request->m_headers[header_names::authorization]);
            VERIFY_ARE_EQUAL(U("/"), request->m_path);
            request->reply(status_codes::OK);
        });

        http_response response = client.request(methods::GET).get();
        VERIFY_ARE_EQUAL(status_codes::OK, response.status_code());
    }

    // Bearer token in query, default access token key (bearer_auth() == false)
    {
        c.set_bearer_auth(false);
        config.set_oauth2(c);

        http_client client(m_uri, config);
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(U(""), request->m_headers[header_names::authorization]);
            VERIFY_ARE_EQUAL(U("/?access_token=12345678"), request->m_path);
            request->reply(status_codes::OK);
        });

        http_response response = client.request(methods::GET).get();
        VERIFY_ARE_EQUAL(status_codes::OK, response.status_code());
    }

    // Bearer token in query, updated token, custom access token key (bearer_auth() == false)
    {
        c.set_bearer_auth(false);
        c.set_access_token_key(U("open"));
        c.set_token(U("Sesame"));
        config.set_oauth2(c);

        http_client client(m_uri, config);
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(U(""), request->m_headers[header_names::authorization]);
            VERIFY_ARE_EQUAL(U("/?open=Sesame"), request->m_path);
            request->reply(status_codes::OK);
        });

        http_response response = client.request(methods::GET).get();
        VERIFY_ARE_EQUAL(status_codes::OK, response.status_code());
    }
}

} // SUITE(oauth2_tests)


}}}}
