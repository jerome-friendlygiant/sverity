// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned Lanes = 8,
  localparam int unsigned CntW = $clog2(Lanes + 1)
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,
  input  logic [Lanes-1:0]     keep_i,
  input  logic                 valid_i,
  output logic [CntW-1:0]      beats_o,
  output logic [Lanes-1:0]     contiguous_mask_o
);

  function automatic logic [CntW-1:0] count_ones(input logic [Lanes-1:0] v);
    logic [CntW-1:0] n;
    n = '0;
    for (int unsigned k = 0; k < Lanes; k++) begin
      if (v[k]) n = n + 1'b1;
    end
    return n;
  endfunction

  function automatic logic [Lanes-1:0] low_mask(input logic [CntW-1:0] n);
    logic [Lanes-1:0] m;
    for (int unsigned k = 0; k < Lanes; k++) begin
      m[k] = (CntW'(k) < n);
    end
    return m;
  endfunction

  logic [CntW-1:0] cnt_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)      cnt_q <= '0;
    else if (valid_i) cnt_q <= count_ones(keep_i);
  end

  assign beats_o           = cnt_q;
  assign contiguous_mask_o = low_mask(cnt_q);

endmodule
