// sverity-args: --top top
// the child body is shared by two instances: one finding with count 2; a call in a condition makes the exclusivity undecidable, so it is still reported
// expect: LNT-025 WARNING 13
// expect-count: LNT-025 WARNING 2
module child(input logic a, b, output logic y);
  logic x;
  function automatic logic chk(logic v);
    return v;
  endfunction
  always_comb begin
    x = 1'b0;
    if (chk(a)) x = 1'b1;
    if (b) x = 1'b0;
    y = x;
  end
endmodule
module top(input logic a, b, output logic y0, y1);
  child u0(a, b, y0);
  child u1(b, a, y1);
endmodule
