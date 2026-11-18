// sverity-args: --top top
// The checker calls a function declared outside the region that writes nothing: still only a checker.
// expect: LNT-083 INFO 7
// expect-none: LNT-083 WARNING
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  function automatic logic [3:0] inc(input logic [3:0] v); return v + 4'd1; endfunction
  // synopsys translate_off
  logic [3:0] seen;
  always @(posedge clk) seen <= inc(d);
  // synopsys translate_on
  always_ff @(posedge clk) q <= d;
endmodule
