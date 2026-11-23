// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// expect: CDC-012 WARNING
module sync2(input logic clk, rst_n, serial_i, output logic serial_o);
  (* ASYNC_REG = "TRUE" *) logic [1:0] reg_q;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) reg_q <= '0;
    else reg_q <= {reg_q[0], serial_i};
  assign serial_o = reg_q[1];
endmodule
module top(input logic clka, clkb, rst_n, d, x, input logic [1:0] sel, output logic pulse_o, output logic o2);
  logic launch, synced, prior, pulse;
  always_ff @(posedge clka) launch <= d;
  sync2 i_sync(.clk(clkb), .rst_n, .serial_i(launch), .serial_o(synced));
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) prior <= 1'b0;
    else prior <= synced;
  assign pulse = synced ^ prior;
  assign pulse_o = pulse;
  function automatic logic f(input logic a, b); return a & b; endfunction
  assign o2 = f(prior, x);
endmodule
