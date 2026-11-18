// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q;
  function automatic logic get(); return q; endfunction
  always @(posedge clk) q = 1;
  always_comb y = get();
endmodule
