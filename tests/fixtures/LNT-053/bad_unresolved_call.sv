// sverity-args: --top top
// a user function between the two assignments might read t: reported Unresolved, not Established
// expect: LNT-053 WARNING 10
module top(input logic a, b, output logic y);
  logic t;
  function automatic logic inv(logic v);
    return ~v;
  endfunction
  always_comb begin
    t = a;
    y = inv(b);
    t = b;
    y = y ^ t;
  end
endmodule
