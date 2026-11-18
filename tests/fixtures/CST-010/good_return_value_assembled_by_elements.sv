// sverity-args: --top top
// A return value written element by element, every element at a constant index, is established when the elements cover it.
// expect-none: CST-010
module top(input logic clk, input logic [3:0] d, output logic [7:0] q);
  function automatic logic [7:0] widen(input logic [3:0] v);
    widen[7:4] = v;
    widen[3:0] = ~v;
  endfunction
  always_ff @(posedge clk) q <= widen(d);
endmodule
