// sverity-args: --top top
// A wired-AND net driven by an exact copy of clk (through a straight-line function) and by a constant 0 has no rising edge: it
// is not a copy of clk, so it stays a clock root of its own.
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
module top(input logic clk, d, output logic a, b);
  function automatic logic [1:0] fanout(input logic c);
    return {c, c};
  endfunction
  wand [1:0] clocks;
  assign clocks = fanout(clk);
  assign clocks[1] = 1'b0;
  always_ff @(posedge clk) a <= d;
  always_ff @(posedge clocks[1]) b <= a;
endmodule
