module top(input logic clk_a, clk_b, rst_n, input logic event_req, output logic [7:0] out);
logic toggle,ack; logic [1:0] ts,as; logic prev_req; logic [7:0] count;
always_ff @(posedge clk_a or negedge rst_n) if(!rst_n) begin toggle<=0;as<=0;prev_req<=0; end else begin as<={as[0],ack}; prev_req<=event_req; if(event_req && !prev_req && toggle==as[1]) toggle<=~toggle; end
always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin ts<=0;ack<=0;count<=0; end else begin ts<={ts[0],toggle}; if(ts[1]!=ack) begin count<=count+1'b1;ack<=ts[1]; end end
assign out=count; endmodule
