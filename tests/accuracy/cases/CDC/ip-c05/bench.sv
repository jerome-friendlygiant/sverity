`timescale 1ns/1ps
module bench;
  logic clk_s=0, clk_d=0, rst_n=0, send=0, accept_d;
  logic [7:0] payload_s=0, payload_d;
  logic valid_d;
  c05 dut(.*);
  always #4 clk_s=~clk_s;
  always #5 clk_d=~clk_d;
  assign accept_d=valid_d;
  initial begin
    #70 rst_n=1;
    repeat (3) @(posedge clk_s);
    @(negedge clk_s); payload_s=8'hA6; send=1;
    @(negedge clk_s); send=0;
    wait(valid_d);
    if (payload_d !== 8'hA6) $fatal(1,"first payload mismatch: %h",payload_d);
    repeat (5) @(posedge clk_s);
    @(negedge clk_s); payload_s=8'h39; send=1;
    @(negedge clk_s); send=0;
    wait(valid_d);
    if (payload_d !== 8'h39) $fatal(1,"second payload mismatch: %h",payload_d);
    repeat (4) @(posedge clk_d);
    $display("C05 handshake schedule passed"); $finish;
  end
  initial begin #2000; $fatal(1,"timeout"); end
endmodule
