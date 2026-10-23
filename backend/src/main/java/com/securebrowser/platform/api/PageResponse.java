package com.securebrowser.platform.api;

import java.util.List;
import org.springframework.data.domain.Page;

/** Representa una página sin exponer la serialización interna de Spring Data. */
public record PageResponse<T>(List<T> items, int page, int size, long totalElements, int totalPages) {
    public PageResponse {
        items = List.copyOf(items);
    }

    public static <T> PageResponse<T> from(Page<T> page) {
        return new PageResponse<>(page.getContent(), page.getNumber(), page.getSize(),
                page.getTotalElements(), page.getTotalPages());
    }
}
