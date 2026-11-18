// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned Width = 24
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             capture_en_i,
  input  logic [Width-1:0] sample_i,
  output logic [Width-1:0] held_o,
  output logic             capture_seen_o
);

  logic gated_clk;
  logic [Width-1:0] held_q;
  logic seen_q;

  clk_gate_cell u_icg (
    .CK (clk_i),
    .E  (capture_en_i),
    .TE (1'b0),
    .Q  (gated_clk)
  );

  always_ff @(posedge gated_clk or negedge rst_ni) begin
    if (!rst_ni) held_q <= '0;
    else         held_q <= sample_i;
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) seen_q <= 1'b0;
    else         seen_q <= seen_q | capture_en_i;
  end

  assign held_o         = held_q;
  assign capture_seen_o = seen_q;

endmodule
