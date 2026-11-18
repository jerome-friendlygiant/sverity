// sverity-args: --top top --option LNT-025.overrides=warning
// Equal literals do not hide an intervening call that reads the variable: whether it saw the first write stays unresolved.
// expect: LNT-025 WARNING
module top(input logic a, b, d, output logic q, z);
  function automatic logic peek(input logic v); return v ^ q; endfunction
  always_comb begin
    q = 1;
    if (a) q = 0;
    z = peek(d);
    if (b) q = 0;
  end
endmodule
