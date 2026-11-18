// sverity-args: --top top
// Lookalike of the `ref` writer: the formal is `const ref`, so the call only reads the register and the region stays a checker.
// expect: LNT-083 INFO
// expect-none: LNT-083 WARNING
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] r;
  function automatic bit full(const ref logic [3:0] x); return x == 4'hf; endfunction
  always_ff @(posedge clk) q <= d + r;
  // synopsys translate_off
  logic seen;
  always @(posedge clk) begin seen <= full(r); if (seen) $display("f"); end
  // synopsys translate_on
endmodule
