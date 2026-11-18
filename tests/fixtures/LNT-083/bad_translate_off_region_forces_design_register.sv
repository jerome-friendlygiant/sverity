// sverity-args: --top top
// force and release on a design register: synthesis builds the register without them.
// expect: LNT-083 WARNING 8
// expect-none: LNT-083 INFO
module top(input logic clk, input logic d, output logic q);
  logic r;
  always_ff @(posedge clk) r <= d;
  // synopsys translate_off
  logic seen;
  always @(posedge clk) begin seen <= d; if (d) force r = 1'b0; else release r; end
  // synopsys translate_on
  assign q = r;
endmodule
