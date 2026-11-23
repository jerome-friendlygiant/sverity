// Request/acknowledge link between two clock domains.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic start_i,
  input  logic dst_ready_i,
  output logic busy_o,
  output logic grant_o
);
  logic req_q;
  logic a1_q;
  logic a2_q;
  logic r1_q;
  logic r2_q;
  logic ack_q;
  logic grant_q;

  always_ff @(posedge clk_a) begin
    if (start_i) req_q <= 1'b1;
    else if (a2_q) req_q <= 1'b0;
  end

  always_ff @(posedge clk_a) begin
    a1_q <= ack_q;
    a2_q <= a1_q;
  end

  always_ff @(posedge clk_b) begin
    r1_q <= req_q;
    r2_q <= r1_q;
  end

  always_ff @(posedge clk_b) ack_q <= dst_ready_i;
  always_ff @(posedge clk_b) grant_q <= r2_q;

  assign busy_o  = req_q;
  assign grant_o = grant_q;
endmodule
