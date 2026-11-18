// sverity-args: --top top
// A write in a called function inside a loop that steps by two: the call site is reported as a driver of the whole vector,
// which says nothing about the bits written.
// expect: LNT-008 WARNING 6
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  function automatic void fill();
    int k = 0;
    while (k < 8) begin v[k] = c; k = k + 2; end
  endfunction
  always_comb fill();
  assign y = v;
endmodule
