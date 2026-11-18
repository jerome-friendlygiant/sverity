// Shared 8-bit data bus pad: driven while oe is set, sampled every cycle.
// Interface:
//   oe      drive enable for the bus
//   wr_data value driven onto dq while oe is set
//   dq      bidirectional data bus
//   rd_q    registered copy of the bus
// When oe is clear the external device owns the bus.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       oe,
  input  logic [7:0] wr_data,
  inout  wire  [7:0] dq,
  output logic [7:0] rd_q,
  output logic       turnaround
);
  logic oe_q;

  assign dq = oe ? wr_data : 8'bz;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      rd_q <= 8'h00;
      oe_q <= 1'b0;
    end else begin
      rd_q <= dq;
      oe_q <= oe;
    end
  end

  // one cycle after the bus is released
  assign turnaround = oe_q & ~oe;
endmodule
