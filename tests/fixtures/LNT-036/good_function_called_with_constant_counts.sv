// sverity-args: --top top
// Every call passes a constant, so `32 - amt` lies in 21..26 and cannot wrap before the shift.
// expect-none: LNT-036
module top(input logic [31:0] a, b, output logic [31:0] r);
  function automatic logic [31:0] rotr(input logic [31:0] x, input integer amt);
    return (x >> amt) | (x << (32 - amt));
  endfunction
  assign r = rotr(a, 6) ^ rotr(b, 11);
endmodule
