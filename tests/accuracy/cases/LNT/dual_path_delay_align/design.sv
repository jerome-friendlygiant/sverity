// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module delay_stage #(
  parameter int Depth = 1,
  parameter int Width = 16
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic [Width-1:0] d_i,
  output logic [Width-1:0] q_o
);

  if (Depth == 1) begin : g_single
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) q_o <= '0;
      else         q_o <= d_i;
    end
  end else begin : g_double
    logic [Width-1:0] mid_q;
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        mid_q <= '0;
        q_o   <= '0;
      end else begin
        mid_q <= d_i;
        q_o   <= mid_q;
      end
    end
  end

endmodule

module top #(
  parameter int Width = 16
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic [Width-1:0] slow_i,
  input  logic [Width-1:0] fast_i,
  output logic [Width:0]   sum_o
);

  logic [Width-1:0] slow_q, fast_q;

  delay_stage #(.Depth(1), .Width(Width)) u_slow (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),
    .d_i    (slow_i),
    .q_o    (slow_q)
  );

  delay_stage #(.Depth(2), .Width(Width)) u_fast (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),
    .d_i    (fast_i),
    .q_o    (fast_q)
  );

  assign sum_o = {1'b0, slow_q} + {1'b0, fast_q};

endmodule
