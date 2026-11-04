// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-017 WARNING
// expect-none: CDC-017 ERROR
// expect-none: CDC-018
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] out);
  logic [3:0] count, encoded, first, second;
  function automatic logic [3:0] transform(input logic [3:0] v);
    for (int i = 0; i < 4; i++) transform[i] = v[i];
  endfunction
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) count <= 0; else if (inc) count <= count + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) encoded <= 0; else encoded <= transform(count);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin first <= 0; second <= 0; end else begin first <= encoded; second <= first; end
  assign out = second;
endmodule
