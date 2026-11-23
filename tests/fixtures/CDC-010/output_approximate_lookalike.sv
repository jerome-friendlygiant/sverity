// sverity-args: --top top --sdc @DIR@/output_approximate_lookalike.sdc
// expect-none: CDC-010
// expect-none: CDC-013
// expect: CST-012 WARNING
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clk_src, clk_dst, input logic a, b, output logic observed);
  logic a1, a2, b1, b2;
  always_ff @(posedge clk_dst) begin
    a1 <= a; a2 <= a1;
    b1 <= b; b2 <= b1;
  end
  function automatic logic first(logic x, logic y);
    return x;
  endfunction
  assign observed = first(a2, b2);
endmodule
