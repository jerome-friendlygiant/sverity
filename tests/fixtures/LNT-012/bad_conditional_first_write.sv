// sverity-args: --top top
// The write before the read does not run on every path, so the read can see the previous value.
// expect: LNT-012 WARNING
module top(input logic a, en, output logic y);
  logic s;
  always_comb begin
    if (en) s = a;
    s = s ^ a;
    y = s;
  end
endmodule
