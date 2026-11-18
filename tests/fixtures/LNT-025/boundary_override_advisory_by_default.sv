// sverity-args: --top top
// By default a later conditional write in one procedure is source-ordered priority: advisory INFO.
// --option LNT-025.overrides=warning raises it (see bad_sibling_if.sv).
// expect: LNT-025 INFO 11
// expect-none: LNT-025 WARNING
module top(input logic a, b, output logic y);
  logic x;
  always_comb begin
    x = 1'b0;
    if (a) x = 1'b1;
    if (b) x = 1'b0;
    y = x;
  end
endmodule
