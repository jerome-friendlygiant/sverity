/*
 * Adapted from verilog-axis rtl/sync_reset.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
 * Copyright (c) 2014-2020 Alex Forencich
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Reduced and modified for the SVerity golden set.
 */

module top #(
  parameter int N = 3
) (
  input logic clk_i, input logic rst_i, output logic out_o
);
  logic [N-1:0] sync_q;
  always_ff @(posedge clk_i or posedge rst_i) begin
    if (rst_i) sync_q <= '1;
    else sync_q <= {sync_q[N-2:0], 1'b0};
  end
  assign out_o = sync_q[N-1];
endmodule
