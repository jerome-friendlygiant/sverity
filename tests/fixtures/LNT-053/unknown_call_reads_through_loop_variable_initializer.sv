// sverity-args: --top top
// The callee reads v through a loop-variable initializer between the writes: reported Unresolved, never decided overwritten.
// expect-unresolved: LNT-053 11
module top(input logic a, input logic [3:0] d, output logic [3:0] q, r);
  logic [3:0] v;
  function automatic logic [3:0] count();
    count = '0;
    for (int k = int'(v); k < 16; k++) count = count + 4'd1;
  endfunction
  always_comb begin
    v = d;
    r = count();
    v = 4'd0;
    if (a) v = 4'd3;
    q = v;
  end
endmodule
