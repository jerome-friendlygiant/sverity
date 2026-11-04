// sverity-args: --top top
// Equal literals do not hide an opaque intervening call: its observation remains unresolved.
// expect: LNT-025 WARNING
module top(input logic a, b, d, output logic q, z);
  function automatic logic invert(input logic v); return !v; endfunction
  always_comb begin
    q = 1;
    if (a) q = 0;
    z = invert(d);
    if (b) q = 0;
  end
endmodule
