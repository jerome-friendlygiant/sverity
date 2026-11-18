// sverity-args: --top top
// The region holds a call of a user function: whether it changes design state is not decided here.
// expect-unresolved: LNT-083 11
module top(input logic a, output logic y);
  logic seen;
  function automatic void note(logic v);
    seen = v;
  endfunction
  always_comb begin
    y = a;
    // synopsys translate_off
    note(a);
    // synopsys translate_on
  end
endmodule
