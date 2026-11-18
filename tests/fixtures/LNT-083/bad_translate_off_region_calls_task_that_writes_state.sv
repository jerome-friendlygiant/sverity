// sverity-args: --top top
// The task is declared outside the region and writes a design counter, so the call inside the region changes design state.
// expect: LNT-083 WARNING 8
// expect-none: LNT-083 INFO
module top(input logic clk, input logic d, output logic [3:0] q);
  logic [3:0] cnt;
  task automatic bump(); cnt = cnt + 4'd1; endtask
  // synopsys translate_off
  logic seen;
  always @(posedge clk) begin seen <= d; bump(); end
  // synopsys translate_on
  always_ff @(posedge clk) q <= cnt;
endmodule
