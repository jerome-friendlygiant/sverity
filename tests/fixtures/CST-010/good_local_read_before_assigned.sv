// sverity-args: --top top
// A local read before it is assigned holds no signal: no dependency of the register is lost, so the call is resolved (LNT-044 reports the read).
// expect-none: CST-010
// expect: LNT-044 WARNING 9
module top(input logic clk, sel, output logic q);
  function automatic logic f(input logic a);
    logic x;
    if (a) x = 1'b1;
    return x;
  endfunction
  always_ff @(posedge clk) q <= f(sel);
endmodule
