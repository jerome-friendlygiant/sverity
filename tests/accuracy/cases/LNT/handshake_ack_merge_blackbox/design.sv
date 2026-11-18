// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        start_i,
  input  logic        local_done_i,
  input  logic [7:0]  cmd_i,
  output logic        busy_o,
  output logic        ack_o
);
  logic ack_w;
  logic busy_q;

  assign ack_w = local_done_i;

  remote_engine_macro u_engine (
    .clk   (clk_i),
    .rst_n (rst_ni),
    .start (start_i),
    .cmd   (cmd_i),
    .ack   (ack_w)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)      busy_q <= 1'b0;
    else if (start_i) busy_q <= 1'b1;
    else if (ack_w)   busy_q <= 1'b0;
  end

  assign busy_o = busy_q;
  assign ack_o  = ack_w;
endmodule
