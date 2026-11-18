// sverity-args: --top top
// Without the opt-in, a pair the analysis cannot decide (a user call between the writes that reads the variable) is Unresolved and keeps
// the WARNING floor of every Unresolved finding.
// expect: LNT-025 WARNING 11
module top(input logic a, b, c, d, output logic y, z);
  function automatic logic mix(logic v); return v ^ y; endfunction
  always_comb begin
    y = 1'b0;
    if (c) y = a;
    z = mix(b);
    if (d) y = b;
  end
endmodule
