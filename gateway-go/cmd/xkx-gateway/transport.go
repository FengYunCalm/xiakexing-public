package main

import (
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
)

func splitRemoteAddr(remoteAddr string) (string, int) {
	host, port, err := net.SplitHostPort(remoteAddr)
	if err != nil {
		return "", 0
	}
	parsedPort, convErr := strconv.Atoi(port)
	if convErr != nil || parsedPort <= 0 {
		return host, 0
	}
	return host, parsedPort
}

func normalizeHost(host string) string {
	host = strings.TrimSpace(strings.ToLower(host))
	if host == "" {
		return ""
	}
	if parsedHost, _, err := net.SplitHostPort(host); err == nil {
		return strings.Trim(strings.ToLower(parsedHost), "[]")
	}
	return strings.Trim(host, "[]")
}

// normalizeAuthority 保留 host:port 形状，用于同源判定（端口必须一致）。
func normalizeAuthority(authority string) string {
	authority = strings.TrimSpace(strings.ToLower(authority))
	if authority == "" {
		return ""
	}
	host, port, err := net.SplitHostPort(authority)
	if err != nil {
		return strings.Trim(authority, "[]")
	}
	return net.JoinHostPort(strings.Trim(host, "[]"), port)
}

func isLoopbackHost(host string) bool {
	host = normalizeHost(host)
	return host == "127.0.0.1" || host == "localhost" || host == "::1"
}

// gatewayCheckOrigin 只放行三类请求：无 Origin 的原生客户端、与请求
// scheme+host+port 完全一致的同源请求、以及请求 Host 本身也是 loopback 的
// 本地自动化请求。不接受的 Origin 一律拒绝，不做通配域或配置化白名单。
func gatewayCheckOrigin(r *http.Request) bool {
	origin := strings.TrimSpace(r.Header.Get("Origin"))
	if origin == "" {
		return true
	}
	originURL, err := url.Parse(origin)
	if err != nil || originURL.Scheme == "" || originURL.Host == "" {
		return false
	}
	requestAuthority := normalizeAuthority(r.Host)
	originAuthority := normalizeAuthority(originURL.Host)
	if requestAuthority == "" || originAuthority == "" {
		return false
	}
	requestScheme := "http"
	if r.TLS != nil {
		requestScheme = "https"
	}
	if !strings.EqualFold(originURL.Scheme, requestScheme) {
		return false
	}
	if originAuthority == requestAuthority {
		return true
	}
	return isLoopbackHost(originURL.Hostname()) && isLoopbackHost(r.Host)
}
