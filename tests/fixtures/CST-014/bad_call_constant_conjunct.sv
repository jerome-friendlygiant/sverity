// sverity-args: --top top --sdc @DIR@/bad_call_constant_conjunct.sdc
// expect: CST-014 WARNING
// expect: CDC-039 WARNING
// expect: CST-010 WARNING
module top(input logic a, b, mode, output logic q);
  logic source_q, side;
  function automatic logic effect();
    side = source_q;
    return source_q;
  endfunction
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode && effect()) q <= source_q;
endmodule
