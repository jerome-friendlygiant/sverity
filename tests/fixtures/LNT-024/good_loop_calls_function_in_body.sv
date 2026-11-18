// sverity-args: --top top
// The output bit i is written from a function of a running state; the loop variable is declared by the loop, so the calls cannot write it.
// expect-none: LNT-024
module top #(parameter int unsigned N = 4) (input logic [7:0] s, output logic [N-1:0] y);
  function automatic logic out_bit(logic [7:0] st); return ^st; endfunction
  function automatic logic [7:0] next_state(logic [7:0] st); return {st[6:0], st[7] ^ st[3]}; endfunction
  always_comb begin
    logic [7:0] st = s;
    for (int unsigned i = 0; i < N; i++) begin
      y[i] = out_bit(st);
      st = next_state(st);
    end
  end
endmodule
