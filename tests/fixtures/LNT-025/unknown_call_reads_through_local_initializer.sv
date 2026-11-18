// sverity-args: --top top
// The callee reads y through a local declaration initializer: the call between the assignments may have read it.
// expect-unresolved: LNT-025 13
module top(input logic a, b, c, d, output logic y, z);
  function automatic logic peek();
    logic t = y;
    return t;
  endfunction
  always_comb begin
    y = 1'b0;
    if (c) y = a;
    z = b ^ peek();
    if (d) y = b;
  end
endmodule
