// sverity-args: --top top
// The result is assigned in full before a loop with a run-time bound: whatever the loop does, every output bit is assigned.
// expect-none: LNT-013
module top(input logic [7:0] k, input logic [3:0] n, output logic [7:0] y);
  function automatic logic [7:0] f(logic [7:0] key_in, logic [3:0] cnt);
    logic [7:0] key_out;
    key_out = key_in;
    for (int unsigned j = 0; j < cnt; j++) key_out = key_out ^ 8'(j);
    return key_out;
  endfunction
  assign y = f(k, n);
endmodule
