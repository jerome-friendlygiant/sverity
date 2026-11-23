// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two request captures and two acknowledge returns share dependencies; endpoint ownership cannot be resolved.
// expect-unresolved: CDC-024
// expect-count: CDC-024 WARNING 2
module top(input logic clka, clkb, rst_n, start, output logic req_out);
  logic req, rm0, rs0, rm1, rs1, ack0, am0, as0, ack1, am1, as1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 0; else if (start && as0 && as1) req <= ~req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin rm0<=0; rs0<=0; rm1<=0; rs1<=0; end else begin rm0<=req; rs0<=rm0; rm1<=req; rs1<=rm1; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin ack0<=0; ack1<=0; end else begin ack0<=rs0; ack1<=rs1; end
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin am0<=0; as0<=0; am1<=0; as1<=0; end else begin am0<=ack0; as0<=am0; am1<=ack1; as1<=am1; end
  assign req_out = req;
endmodule
