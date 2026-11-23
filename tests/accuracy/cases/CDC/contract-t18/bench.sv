module tb_handshake;
  logic src_clk=0, dst_clk=0, rst_n=0, send=0;
  logic [7:0] payload_in=0, dst_payload;
  logic src_ack;
  integer captures=0;
  logic captured=0;
  always #5 src_clk=~src_clk;
  always #7 dst_clk=~dst_clk;
  top_cdc_rfc1_t18 dut(.*);
  always @(posedge dst_clk) if (rst_n && dut.ack_dst && !captured) begin
    captured <= 1;
    captures <= captures+1;
    assert(dst_payload == 8'hA6) else $error("incoherent payload");
  end
  initial begin
    repeat (3) @(posedge src_clk); rst_n=1;
    @(negedge src_clk); payload_in=8'hA6; send=1;
    @(negedge src_clk); send=0;
    wait(src_ack);
    repeat (3) @(posedge dst_clk);
    assert(captures == 1) else $error("transaction not delivered exactly once");
    assert(dst_payload == 8'hA6) else $error("payload mismatch");
    $finish;
  end
endmodule
