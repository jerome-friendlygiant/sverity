// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module slice_stage #(
  parameter int W = 8
) (
  input  logic         clk_i,
  input  logic         rst_ni,
  input  logic [W-1:0] d_i,
  input  logic         v_i,
  output logic         r_o,
  output logic [W-1:0] d_o,
  output logic         v_o,
  input  logic         r_i
);
  logic [W-1:0] d_q;
  logic         v_q;

  assign r_o = ~v_q | r_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      v_q <= 1'b0;
      d_q <= '0;
    end else if (r_o) begin
      v_q <= v_i;
      if (v_i) d_q <= d_i;
    end
  end

  assign d_o = d_q;
  assign v_o = v_q;
endmodule

module top #(
  parameter int W = 8,
  parameter int N = 3
) (
  input  logic         clk_i,
  input  logic         rst_ni,
  input  logic [W-1:0] s_data_i,
  input  logic         s_valid_i,
  output logic         s_ready_o,
  output logic [W-1:0] m_data_o,
  output logic         m_valid_o,
  input  logic         m_ready_i
);
  logic [W-1:0] data_c [N+1];
  logic         valid_c [N+1];
  logic         ready_c [N+1];

  assign data_c[0]  = s_data_i;
  assign valid_c[0] = s_valid_i;
  assign s_ready_o  = ready_c[0];
  assign ready_c[N] = m_ready_i;
  assign m_data_o   = data_c[N];
  assign m_valid_o  = valid_c[N];

  for (genvar i = 0; i < N; i++) begin : gen_stage
    slice_stage #(.W(W)) u_stage (
      .clk_i, .rst_ni,
      .d_i(data_c[i]), .v_i(valid_c[i]), .r_o(ready_c[i]),
      .d_o(data_c[i+1]), .v_o(valid_c[i+1]), .r_i(ready_c[i+1])
    );
  end
endmodule
