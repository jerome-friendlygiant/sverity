/*
 *  PicoSoC - A simple example SoC using PicoRV32
 *
 *  Copyright (C) 2017  Claire Xenia Wolf <claire@yosyshq.com>
 *  Source path: picosoc/simpleuart.v @ef203c2b0a3fb793280f5114941416c425c5b461
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS.
 */
// Adapted from picosoc/simpleuart.v.
// Reduced and modified for the SVerity golden set.
module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        load_i,
  input  logic [31:0] divisor_i,
  input  logic        read_i,
  output logic [31:0] divisor_o
);
  logic [31:0] divisor_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      divisor_q <= 32'd0;
    else if (load_i)
      divisor_q <= divisor_i;
  end

  assign divisor_o = divisor_q;
  always_comb begin
    if (read_i)
      divisor_o = divisor_q;
    else
      divisor_o = divisor_q;
  end
endmodule
