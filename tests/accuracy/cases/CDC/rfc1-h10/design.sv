// Request pulse input brought into the consumer clock domain.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic req_pulse_i,
  output logic req_o
);
  logic p1_q;
  logic p2_q;
  logic req_q;

  always_ff @(posedge dst_clk) begin
    p1_q <= req_pulse_i;
    p2_q <= p1_q;
  end

  always_ff @(posedge dst_clk) req_q <= p2_q;

  assign req_o = req_q;
endmodule
