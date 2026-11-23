module top(input logic clk_a, clk_b, rst_n, input logic send, input logic [7:0] payload_in, output logic [7:0] out);
logic [7:0] live; logic req,ack; logic [1:0] rq,ak; logic [7:0] captured;
always_ff @(posedge clk_a or negedge rst_n) if(!rst_n) begin live<=0;req<=0;ak<=0; end else begin live<=payload_in;ak<={ak[0],ack}; if(send && req==ak[1]) req<=~req; end
always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin rq<=0;ack<=0;captured<=0; end else begin rq<={rq[0],req}; if(rq[1]!=ack) begin captured<=live;ack<=rq[1]; end end
assign out=captured; endmodule
